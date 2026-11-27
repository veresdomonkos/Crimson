#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <crimson/core/engine_resources.hpp>
#include <crimson/project/project.hpp>
#include <crimson/project/shader_includes.hpp>

#include "support/temp_folder.hpp"

#ifdef _WIN32
    #include <windows.h>
#endif

using namespace crimson;
using crimson::tests::TempFolder;
using Catch::Matchers::ContainsSubstring;

namespace fs = std::filesystem;

static std::string ReadFile(const fs::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

static void WriteFile(const fs::path& file, std::string_view text)
{
    std::ofstream(file, std::ios::binary) << text;
}

TEST_CASE("Creating a project makes the folder, the file and the assets folder", "[project]")
{
    const TempFolder temp;

    const auto project = Project::Create(temp.Path(), "Game");

    REQUIRE(project);
    CHECK(project->Name() == "Game");
    CHECK(project->Root() == (temp.Path() / "Game").lexically_normal());
    CHECK(fs::is_regular_file(project->Root() / Project::kFileName));
    CHECK(fs::is_directory(project->AssetsDir()));
}

TEST_CASE("A created project can be loaded again", "[project]")
{
    const TempFolder temp;
    const fs::path root = Project::Create(temp.Path(), "Game")->Root();

    // The suffix is what varies, as the temp folder is a new one for each value
    const std::string suffix = GENERATE(as<std::string>{}, "", "/project.crimson", "/");

    const auto loaded = Project::Load(root.string() + suffix);

    CAPTURE(suffix);
    REQUIRE(loaded);
    CHECK(loaded->Name() == "Game");
    CHECK(loaded->Root() == root);
}

TEST_CASE("Creating a project works with non-ASCII names", "[project]")
{
    const TempFolder temp;
    const std::string name = "J\xC3\xA1t\xC3\xA9k";

    const auto created = Project::Create(temp.Path(), name);
    REQUIRE(created);

    const auto loaded = Project::Load(created->Root());
    REQUIRE(loaded);
    CHECK(loaded->Name() == name);
}

TEST_CASE("Creating a project refuses a folder that has something in it", "[project]")
{
    const TempFolder temp;
    temp.Write("Game/precious.txt", "keep me");

    const auto project = Project::Create(temp.Path(), "Game");

    REQUIRE_FALSE(project);
    CHECK_THAT(project.error(), ContainsSubstring("isn't empty"));
    CHECK(fs::exists(temp.Path() / "Game" / "precious.txt"));
    CHECK_FALSE(fs::exists(temp.Path() / "Game" / Project::kFileName));
}

TEST_CASE("Creating a project can use an existing empty folder", "[project]")
{
    const TempFolder temp;
    fs::create_directories(temp.Path() / "Game");

    CHECK(Project::Create(temp.Path(), "Game"));
}

TEST_CASE("Loading explains what is wrong", "[project]")
{
    const TempFolder temp;

    SECTION("No project file")
    {
        const auto project = Project::Load(temp.Path());
        REQUIRE_FALSE(project);
        CHECK_THAT(project.error(), ContainsSubstring("No project.crimson"));
    }

    SECTION("A folder that doesn't exist")
    {
        CHECK_FALSE(Project::Load(temp.Path() / "nothing" / "here"));
    }

    SECTION("Broken JSON")
    {
        temp.Write(Project::kFileName, "{ not json");
        const auto project = Project::Load(temp.Path());
        REQUIRE_FALSE(project);
        CHECK_THAT(project.error(), ContainsSubstring("Invalid"));
    }

    SECTION("No name")
    {
        temp.Write(Project::kFileName, R"({ "version": 1 })");
        CHECK_FALSE(Project::Load(temp.Path()));
    }

    SECTION("A newer version than the engine knows")
    {
        temp.Write(Project::kFileName, R"({ "name": "Future", "version": 999 })");
        const auto project = Project::Load(temp.Path());
        REQUIRE_FALSE(project);
        CHECK_THAT(project.error(), ContainsSubstring("Unsupported"));
    }
}

TEST_CASE("Project names have to work as folder names", "[project]")
{
    SECTION("Accepted")
    {
        const std::string name = GENERATE("Game", "My Game", "game-2_final", "con1", "a.b", "J\xC3\xA1t\xC3\xA9k");
        CAPTURE(name);
        CHECK(Project::ValidateName(name).empty());
    }

    SECTION("Rejected")
    {
        const std::string name = GENERATE("", "a/b", "a\\b", "a:b", "a*b", "a?b", "a\"b", "a<b", "a>b", "a|b", "tab\tname", "dot.", "space ", " space", "CON", "con", "NUL", "COM1", "lpt9");
        CAPTURE(name);
        CHECK_FALSE(Project::ValidateName(name).empty());
    }
}

TEST_CASE("Creating a project validates its name", "[project]")
{
    const TempFolder temp;

    CHECK_FALSE(Project::Create(temp.Path(), "../escape"));
    CHECK_FALSE(fs::exists(temp.Path().parent_path() / "escape"));
}

TEST_CASE("Projects get copies of the engine's shader headers for code editors", "[project]")
{
    const TempFolder temp;
    const auto project = Project::Create(temp.Path(), "Game");
    REQUIRE(project);

    const fs::path folder = project->DataDir() / "shader_includes";
    const fs::path config = project->Root() / "shadertoolsconfig.json";

    const auto readConfig = [&] { return nlohmann::json::parse(ReadFile(config)); };

    SECTION("The headers, and the config pointing at them")
    {
        ExportShaderIncludes(*project);

        const EmbeddedFile* bindings = FindEmbeddedFile(GetEngineResources(), "shaders/crimson/bindings.hlsli");
        REQUIRE(bindings);
        CHECK(ReadFile(folder / "crimson/bindings.hlsli") == bindings->Text());
        CHECK(fs::exists(folder / "crimson/lighting.hlsli"));

        // Shaders are assets, not headers
        CHECK_FALSE(fs::exists(folder / "S_Lit.hlsl"));
        CHECK_FALSE(fs::exists(folder / "internal"));

        CHECK(readConfig().at("hlsl.additionalIncludeDirectories") == nlohmann::json::array({ ".crimson/shader_includes" }));
    }

    SECTION("Out of date and leftover copies are fixed, and the config isn't added to twice")
    {
        ExportShaderIncludes(*project);
        WriteFile(folder / "crimson/bindings.hlsli", "changed");
        WriteFile(folder / "crimson/gone.hlsli", "no longer in the engine");

        ExportShaderIncludes(*project);

        CHECK(ReadFile(folder / "crimson/bindings.hlsli") != "changed");
        CHECK_FALSE(fs::exists(folder / "crimson/gone.hlsli"));
        CHECK(readConfig().at("hlsl.additionalIncludeDirectories").size() == 1);
    }

    SECTION("A config the user made keeps what is in it")
    {
        WriteFile(config, R"({ "hlsl.preprocessorDefinitions": { "MINE": "1" }, "hlsl.additionalIncludeDirectories": [ "mine" ] })");

        ExportShaderIncludes(*project);

        const nlohmann::json json = readConfig();
        CHECK(json.at("hlsl.preprocessorDefinitions").at("MINE") == "1");
        CHECK(json.at("hlsl.additionalIncludeDirectories") == nlohmann::json::array({ "mine", ".crimson/shader_includes" }));
    }

    SECTION("A config that isn't JSON is left alone")
    {
        WriteFile(config, "not json");

        ExportShaderIncludes(*project);

        CHECK(ReadFile(config) == "not json");
        CHECK(fs::exists(folder / "crimson/bindings.hlsli"));
    }
}

TEST_CASE("The data folder is made when needed, and hidden on Windows", "[project]")
{
    const TempFolder temp;
    const auto project = Project::Create(temp.Path(), "Game");
    REQUIRE(project);

    CHECK_FALSE(fs::exists(project->DataDir()));
    REQUIRE(project->EnsureDataDir());
    CHECK(fs::is_directory(project->DataDir()));

#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(project->DataDir().c_str());
    REQUIRE(attributes != INVALID_FILE_ATTRIBUTES);
    CHECK((attributes & FILE_ATTRIBUTE_HIDDEN) != 0);
#endif

    // Again on a folder that is already there
    CHECK(project->EnsureDataDir());
}

TEST_CASE("A project's main scene is kept in the project file", "[project]")
{
    const TempFolder temp;
    auto project = Project::Create(temp.Path(), "Game");
    REQUIRE(project);

    const fs::path file = project->Root() / Project::kFileName;
    const AssetID id = AssetID::FromString("3d2215de-4684-4f85-b18d-c0fb98cd2d47");

    SECTION("A new project has none")
    {
        CHECK_FALSE(project->GetMainScene().Id);
        CHECK_FALSE(nlohmann::json::parse(ReadFile(file)).contains("mainScene"));
    }

    SECTION("It is written by id and path, and loaded again")
    {
        REQUIRE(project->SetMainScene(id, "scenes/demo.scene"));

        const nlohmann::json json = nlohmann::json::parse(ReadFile(file));
        CHECK(json.at("version") == 1);
        CHECK(json.at("mainScene").at("guid") == id.ToString());
        CHECK(json.at("mainScene").at("path") == "scenes/demo.scene");

        const auto loaded = Project::Load(project->Root());
        REQUIRE(loaded);
        CHECK(loaded->GetMainScene().Id == id);
        CHECK(loaded->GetMainScene().Path == "scenes/demo.scene");
    }

    SECTION("An empty id clears it")
    {
        REQUIRE(project->SetMainScene(id, "scenes/demo.scene"));
        REQUIRE(project->SetMainScene({}, {}));

        CHECK_FALSE(project->GetMainScene().Id);
        CHECK_FALSE(nlohmann::json::parse(ReadFile(file)).contains("mainScene"));
    }

    SECTION("A hand-written one is loaded")
    {
        temp.Write("Game/project.crimson", R"({ "name": "Game", "version": 1,
            "mainScene": { "guid": "3d2215de-4684-4f85-b18d-c0fb98cd2d47", "path": "scenes/demo.scene" } })");

        const auto loaded = Project::Load(project->Root());
        REQUIRE(loaded);
        CHECK(loaded->GetMainScene().Id == id);
        CHECK(loaded->GetMainScene().Path == "scenes/demo.scene");
    }

    SECTION("One that is wrong is ignored, and doesn't stop the project from opening")
    {
        const std::string_view text = GENERATE(as<std::string_view>{},
            R"({ "name": "Game", "version": 1, "mainScene": { "guid": "nonsense", "path": 3 } })",
            R"({ "name": "Game", "version": 1, "mainScene": 42 })",
            R"({ "name": "Game", "version": 1, "mainScene": [ "a" ] })");
        CAPTURE(text);
        temp.Write("Game/project.crimson", text);

        const auto loaded = Project::Load(project->Root());
        REQUIRE(loaded);
        CHECK_FALSE(loaded->GetMainScene().Id);
        CHECK(loaded->GetMainScene().Path.empty());
    }

    SECTION("A file that can't be written keeps the old one")
    {
        REQUIRE(project->SetMainScene(id, "scenes/demo.scene"));

        // A folder where the project file is makes the write fail on every platform
        fs::remove(file);
        fs::create_directory(file);

        CHECK_FALSE(project->SetMainScene({}, {}));
        CHECK(project->GetMainScene().Id == id);
    }
}
