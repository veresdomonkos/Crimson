#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include <crimson_editor/platform.hpp>

namespace fs = std::filesystem;
namespace platform = crimson::editor::platform;

TEST_CASE("An environment variable that isn't set is an empty path", "[platform]")
{
    CHECK(platform::EnvironmentPath("CRIMSON_TEST_SURELY_UNSET").empty());
}

TEST_CASE("The home folder exists", "[platform]")
{
    const fs::path home = platform::HomeFolder();
    CHECK_FALSE(home.empty());
    CHECK(fs::is_directory(home));
}

TEST_CASE("The user config folder is Crimson's own", "[platform]")
{
#ifdef _WIN32
    CHECK(platform::UserConfigFolder().filename() == "Crimson");
#else
    CHECK(platform::UserConfigFolder().filename() == "crimson");
#endif
}

TEST_CASE("The user cache folder is Crimson's own", "[platform]")
{
#ifdef _WIN32
    CHECK(platform::UserCacheFolder().filename() == "Cache");
    CHECK(platform::UserCacheFolder().parent_path().filename() == "Crimson");
#else
    CHECK(platform::UserCacheFolder().filename() == "crimson");
#endif
}
