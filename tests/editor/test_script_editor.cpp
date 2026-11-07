#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include <crimson_editor/editor_settings.hpp>
#include <crimson_editor/script_editor.hpp>
#include <crimson_editor/system_shell.hpp>

#include "support/temp_folder.hpp"

using namespace crimson::editor;
using crimson::tests::TempFolder;

namespace fs = std::filesystem;

namespace
{
    // As the platform writes the path, which is what the editor is given
    std::string Native(const fs::path& path)
    {
        return PathToUtf8(path.lexically_normal().make_preferred());
    }
}

TEST_CASE("Script editor arguments are split like a shell would, then filled in", "[script editor]")
{
    const fs::path file = fs::path("/projects/My Game/assets/shaders/S_Water.hlsl");
    const fs::path project = fs::path("/projects/My Game");

    SECTION("VS Code's: the project, then the file at its line")
    {
        const std::vector<std::string> expected = { Native(project), "-g", Native(file) + ":12" };
        CHECK(ExpandScriptEditorArguments(R"ARGS("$(ProjectPath)" -g "$(File)":$(Line))ARGS", file, 12, project) == expected);
    }

    SECTION("A path with spaces stays one argument, quoted or not")
    {
        CHECK(ExpandScriptEditorArguments("$(File)", file, 1, project) == std::vector{ Native(file) });
        CHECK(ExpandScriptEditorArguments(R"ARGS("$(File)")ARGS", file, 1, project) == std::vector{ Native(file) });
    }

    SECTION("Spaces between arguments don't make empty ones, but quotes do")
    {
        CHECK(ExpandScriptEditorArguments("  --line   $(Line)  ", file, 3, project) == std::vector<std::string>{ "--line", "3" });
        CHECK(ExpandScriptEditorArguments(R"ARGS(-a "" -b)ARGS", file, 1, project) == std::vector<std::string>{ "-a", "", "-b" });
    }

    SECTION("A placeholder can be in an argument more than once, and the line is never below 1")
    {
        CHECK(ExpandScriptEditorArguments("$(Line)-$(Line)", file, 0, project) == std::vector<std::string>{ "1-1" });
    }

    SECTION("No arguments gives none")
    {
        CHECK(ExpandScriptEditorArguments("", file, 1, project).empty());
    }
}

TEST_CASE("Known script editors get their own arguments", "[script editor]")
{
    CHECK(DefaultScriptEditorArguments("C:/Users/me/AppData/Local/Programs/Microsoft VS Code/Code.exe") == R"ARGS("$(ProjectPath)" -g "$(File)":$(Line))ARGS");
    CHECK(DefaultScriptEditorArguments("/usr/bin/code") == R"ARGS("$(ProjectPath)" -g "$(File)":$(Line))ARGS");
    CHECK(DefaultScriptEditorArguments("C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/devenv.exe") == R"ARGS(/edit "$(File)")ARGS");
    CHECK(DefaultScriptEditorArguments("C:/Program Files/JetBrains/CLion 2026.2.3/bin/clion64.exe") == R"ARGS(--line $(Line) "$(File)")ARGS");
    CHECK(DefaultScriptEditorArguments("/opt/rider/bin/rider") == R"ARGS(--line $(Line) "$(File)")ARGS");
    CHECK(DefaultScriptEditorArguments("/usr/bin/gedit") == R"ARGS("$(File)")ARGS");
}

TEST_CASE("Windows command line arguments are quoted to be read back as they were", "[script editor]")
{
    CHECK(QuoteWindowsArgument("plain") == "plain");
    CHECK(QuoteWindowsArgument("") == "\"\"");
    CHECK(QuoteWindowsArgument("C:\\My Game\\a.hlsl") == "\"C:\\My Game\\a.hlsl\"");

    // A quote is escaped, and so are the backslashes right before it or before the closing quote
    CHECK(QuoteWindowsArgument("say \"hi\"") == "\"say \\\"hi\\\"\"");
    CHECK(QuoteWindowsArgument("C:\\Folder With Space\\") == "\"C:\\Folder With Space\\\\\"");
    CHECK(QuoteWindowsArgument("a\\\"b c") == "\"a\\\\\\\"b c\"");
}

TEST_CASE("The script editor is remembered between runs", "[script editor]")
{
    const TempFolder temp;
    const fs::path file = temp.Path() / "settings.json";

    const EditorSettings::ScriptEditor editor{ .Program = fs::path(u8"C:/Program Files/\u00C9diteur/code.exe"), .Arguments = R"ARGS(-g "$(File)":$(Line))ARGS" };

    {
        EditorSettings settings(file);
        CHECK(settings.GetScriptEditor() == EditorSettings::ScriptEditor{});

        settings.SetScriptEditor(editor);
    }

    CHECK(EditorSettings(file).GetScriptEditor() == editor);

    SECTION("Going back to the system default is remembered too")
    {
        EditorSettings(file).SetScriptEditor({});
        CHECK(EditorSettings(file).GetScriptEditor().Program.empty());
    }

    SECTION("A broken file starts from the defaults")
    {
        temp.Write("settings.json", "{ not json");
        CHECK(EditorSettings(file).GetScriptEditor() == EditorSettings::ScriptEditor{});
    }
}

TEST_CASE("Showing the engine's assets is remembered between runs, beside the script editor", "[script editor]")
{
    const TempFolder temp;
    const fs::path file = temp.Path() / "settings.json";

    const EditorSettings::ScriptEditor editor{ .Program = fs::path("code.exe") };

    {
        EditorSettings settings(file);
        CHECK_FALSE(settings.GetShowEngineContent());

        settings.SetScriptEditor(editor);
        settings.SetShowEngineContent(true);
    }

    const EditorSettings again(file);
    CHECK(again.GetShowEngineContent());
    CHECK(again.GetScriptEditor() == editor);
}
