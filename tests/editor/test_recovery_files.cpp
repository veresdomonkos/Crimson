#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <crimson_editor/recovery_files.hpp>

#include "support/temp_folder.hpp"

using crimson::editor::RecoveryFiles;
using crimson::tests::TempFolder;

namespace fs = std::filesystem;

static std::string ReadFile(const fs::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

TEST_CASE("A copy is written to its folder and read back", "[recovery files]")
{
    const TempFolder temp;
    const fs::path folder = temp.Path() / "recovery";
    REQUIRE_FALSE(fs::exists(folder));

    RecoveryFiles files(folder);
    files.Write("a.scene", "one");

    CHECK(files.Read("a.scene") == "one");
    CHECK(files.WrittenAt("a.scene").has_value());
    CHECK(fs::is_directory(folder));

    for (const auto& entry : fs::directory_iterator(folder))
        CHECK(entry.path().extension() != ".saving");
}

TEST_CASE("The latest write of a name wins", "[recovery files]")
{
    const TempFolder temp;
    fs::path file;
    {
        RecoveryFiles files(temp.Path() / "recovery");
        file = files.Folder() / "a.scene";

        files.Write("a.scene", "one");
        files.Write("a.scene", "two");
        files.Write("a.scene", "three");

        CHECK(files.Read("a.scene") == "three");
    }

    CHECK(ReadFile(file) == "three");
}

TEST_CASE("Removing a copy drops a write still waiting", "[recovery files]")
{
    const TempFolder temp;
    const std::string big(4 * 1024 * 1024, 'x');
    fs::path folder;
    {
        RecoveryFiles files(temp.Path() / "recovery");
        folder = files.Folder();

        files.Write("a.scene", big);
        files.Write("b.scene", big);
        files.Write("c.scene", big);
        files.Write("d.scene", big);

        files.Remove("c.scene");
        CHECK_FALSE(files.WrittenAt("c.scene").has_value());
    }

    CHECK_FALSE(fs::exists(folder / "c.scene"));
    CHECK(fs::exists(folder / "a.scene"));
    CHECK(fs::exists(folder / "b.scene"));
    CHECK(fs::exists(folder / "d.scene"));
}

TEST_CASE("Writes still waiting are done when it is destroyed", "[recovery files]")
{
    const TempFolder temp;
    const fs::path folder = temp.Path() / "recovery";
    {
        RecoveryFiles files(folder);
        files.Write("a.scene", "one");
        files.Write("b.scene", "two");
    }

    CHECK(ReadFile(folder / "a.scene") == "one");
    CHECK(ReadFile(folder / "b.scene") == "two");
}

TEST_CASE("A missing copy is nothing", "[recovery files]")
{
    const TempFolder temp;
    RecoveryFiles files(temp.Path() / "recovery");

    CHECK_FALSE(files.Read("a.scene").has_value());
    CHECK_FALSE(files.WrittenAt("a.scene").has_value());

    files.Remove("a.scene");
}
