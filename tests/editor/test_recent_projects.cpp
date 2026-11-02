#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <filesystem>
#include <string>

#include <crimson_editor/recent_projects.hpp>

#include "support/temp_folder.hpp"

using namespace crimson;
using crimson::editor::RecentProjects;
using crimson::tests::TempFolder;

namespace fs = std::filesystem;

TEST_CASE("The recent projects are empty on a first start", "[recent projects]")
{
    const TempFolder temp;
    const RecentProjects recents(temp.Path() / "recent.json");

    CHECK(recents.GetEntries().empty());
}

TEST_CASE("A project's last scene is remembered", "[recent projects]")
{
    const TempFolder temp;
    const fs::path file = temp.Path() / "recent.json";
    const Project project = *Project::Create(temp.Path(), "A");
    const AssetID scene = AssetID::Generate();

    {
        RecentProjects recents(file);
        recents.Add(project);
        CHECK_FALSE(recents.GetLastScene(project.Root()));

        recents.SetLastScene(project.Root(), scene);
        recents.SetLastScene(temp.Path() / "not listed", AssetID::Generate());

        // Opening it again is no reason to forget
        recents.Add(project);
        CHECK(recents.GetLastScene(project.Root()) == scene);
    }

    CHECK(RecentProjects(file).GetLastScene(project.Root()) == scene);
}

TEST_CASE("The most recently added project comes first", "[recent projects]")
{
    const TempFolder temp;
    const fs::path file = temp.Path() / "recent.json";

    const Project a = *Project::Create(temp.Path(), "A");
    const Project b = *Project::Create(temp.Path(), "B");

    RecentProjects recents(file);
    recents.Add(a);
    recents.Add(b);

    REQUIRE(recents.GetEntries().size() == 2);
    CHECK(recents.GetEntries()[0].Name == "B");
    CHECK(recents.GetEntries()[1].Name == "A");

    SECTION("Adding one again moves it up instead of listing it twice")
    {
        recents.Add(a);

        REQUIRE(recents.GetEntries().size() == 2);
        CHECK(recents.GetEntries()[0].Name == "A");
    }

    SECTION("Removing one only forgets it")
    {
        recents.Remove(a.Root());

        REQUIRE(recents.GetEntries().size() == 1);
        CHECK(recents.GetEntries()[0].Name == "B");
        CHECK(fs::exists(a.Root() / Project::kFileName));
    }

    SECTION("Removing one that isn't listed changes nothing")
    {
        recents.Remove(temp.Path() / "other");
        CHECK(recents.GetEntries().size() == 2);
    }
}

// The list was once silently empty on every start, parsed into a temporary that died before the loop ran
TEST_CASE("The recent projects survive a restart", "[recent projects]")
{
    const TempFolder temp;
    const fs::path file = temp.Path() / "recent.json";

    const Project a = *Project::Create(temp.Path(), "A");
    const Project b = *Project::Create(temp.Path(), "J\xC3\xA1t\xC3\xA9k");

    {
        RecentProjects recents(file);
        recents.Add(a);
        recents.Add(b);
    }

    const RecentProjects reloaded(file);

    REQUIRE(reloaded.GetEntries().size() == 2);
    CHECK(reloaded.GetEntries()[0].Name == b.Name());
    CHECK(reloaded.GetEntries()[0].Path == b.Root());
    CHECK(reloaded.GetEntries()[0].LastOpened > 0);
    CHECK(reloaded.GetEntries()[1].Path == a.Root());
}

TEST_CASE("Entries load sorted by when they were last opened", "[recent projects]")
{
    const TempFolder temp;
    const fs::path file = temp.Write("recent.json", R"({ "projects": [
        { "name": "Old", "path": "old", "lastOpened": 100 },
        { "name": "New", "path": "new", "lastOpened": 300 },
        { "name": "Middle", "path": "middle", "lastOpened": 200 }
    ] })");

    const RecentProjects recents(file);

    REQUIRE(recents.GetEntries().size() == 3);
    CHECK(recents.GetEntries()[0].Name == "New");
    CHECK(recents.GetEntries()[1].Name == "Middle");
    CHECK(recents.GetEntries()[2].Name == "Old");
}

TEST_CASE("A damaged list is ignored", "[recent projects]")
{
    const TempFolder temp;

    const std::string contents = GENERATE(
        "{ not json",
        "[]",
        R"({ "projects": 5 })",
        R"({ "projects": [ { "path": "no name" } ] })");

    const RecentProjects recents(temp.Write("recent.json", contents));

    CHECK(recents.GetEntries().empty());
}

TEST_CASE("A project that is gone stays listed", "[recent projects]")
{
    const TempFolder temp;
    const fs::path file = temp.Path() / "recent.json";

    const Project project = *Project::Create(temp.Path(), "Moved");

    {
        RecentProjects recents(file);
        recents.Add(project);
    }

    fs::remove_all(project.Root());

    // The project manager shows it as missing, so that it can be removed by hand
    CHECK(RecentProjects(file).GetEntries().size() == 1);
}
