#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include <crimson_editor/system_shell.hpp>

namespace shell = crimson::editor::shell;

TEST_CASE("A path as a file URI keeps what is safe in it and encodes the rest", "[system shell]")
{
    CHECK(shell::FileUri(std::filesystem::path("/home/user/project/shaders/S_Water.hlsl")) == "file:///home/user/project/shaders/S_Water.hlsl");
    CHECK(shell::FileUri(std::filesystem::path("/a-b_c.d~e")) == "file:///a-b_c.d~e");

    // Spaces and other bytes become %XX, and a character outside ASCII is encoded as its UTF-8 bytes
    CHECK(shell::FileUri(std::filesystem::path(u8"/home/a b/\u00FC.hlsl")) == "file:///home/a%20b/%C3%BC.hlsl");
    CHECK(shell::FileUri(std::filesystem::path("/50%#?")) == "file:///50%25%23%3F");

    // A path that doesn't start with a slash still gets the slash the URI's path needs
    CHECK(shell::FileUri(std::filesystem::path("relative/file.obj")).starts_with("file:///relative/"));
}
