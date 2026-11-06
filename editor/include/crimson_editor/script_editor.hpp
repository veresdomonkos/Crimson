#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "crimson_editor/editor_settings.hpp"

namespace crimson::editor
{
    // A code editor found on this machine, with the arguments it opens a file with
    struct CodeEditor
    {
        std::string Name;
        std::filesystem::path Program;
        std::string Arguments;
    };

    // VS Code, Visual Studio, Rider and CLion, where they are usually installed or on the PATH
    [[nodiscard]] std::vector<CodeEditor> FindCodeEditors();

    // What a program is given to open a file at a line, going by its name: the known editors get their own,
    // anything else the file alone
    [[nodiscard]] std::string DefaultScriptEditorArguments(const std::filesystem::path& program);

    // Splits arguments the way a shell would, with quotes keeping spaces in one argument, then fills in $(File),
    // $(Line) and $(ProjectPath) in each. A path with spaces stays one argument, quoted or not.
    [[nodiscard]] std::vector<std::string> ExpandScriptEditorArguments(const std::string& arguments, const std::filesystem::path& file,
                                                                       int line, const std::filesystem::path& project);

    // Opens a code file in the script editor, or in the system's program for it when there is none or it can't start
    void OpenInScriptEditor(const EditorSettings::ScriptEditor& editor, const std::filesystem::path& file, const std::filesystem::path& project,
                            int line = 1);
}
