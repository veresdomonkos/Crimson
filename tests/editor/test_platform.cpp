#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string_view>

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

TEST_CASE("Program files have extensions only on Windows", "[platform]")
{
#ifdef _WIN32
    CHECK(platform::ProgramExtensions() == "exe,cmd,bat");
#else
    CHECK(platform::ProgramExtensions().empty());
#endif
}

TEST_CASE("Code editor candidates have names", "[platform]")
{
    // The paths may be empty or missing, as nothing says the editors are installed
    for (const auto& [name, program] : platform::CodeEditorCandidates())
        CHECK_FALSE(name.empty());
}
