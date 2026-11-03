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

using namespace crimson;
using crimson::tests::TempFolder;
using Catch::Matchers::ContainsSubstring;

namespace fs = std::filesystem;

namespace
{
    std::string ReadFile(const fs::path& file)
    {
        std::ifstream stream(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }

    void WriteFile(const fs::path& file, std::string_view text)
    {
        std::ofstream(file, std::ios::binary) << text;
    }
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

    const fs::path folder = project->DataDir() / "shader-includes";
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
        CHECK_FALSE(fs::exists(folder / "main.hlsl"));

        CHECK(readConfig().at("hlsl.additionalIncludeDirectories") == nlohmann::json::array({ ".crimson/shader-includes" }));
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
        CHECK(json.at("hlsl.additionalIncludeDirectories") == nlohmann::json::array({ "mine", ".crimson/shader-includes" }));
    }

    SECTION("A config that isn't JSON is left alone")
    {
        WriteFile(config, "not json");

        ExportShaderIncludes(*project);

        CHECK(ReadFile(config) == "not json");
        CHECK(fs::exists(folder / "crimson/bindings.hlsli"));
    }
}
