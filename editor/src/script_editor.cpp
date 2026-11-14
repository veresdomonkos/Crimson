#include "crimson_editor/script_editor.hpp"

#include <algorithm>
#include <cctype>
#include <system_error>

#include <crimson/core/log.hpp>

#include "crimson_editor/platform.hpp"
#include "crimson_editor/system_shell.hpp"

namespace fs = std::filesystem;

namespace crimson::editor
{
    namespace
    {
        constexpr std::string_view kVSCodeArguments = R"ARGS("$(ProjectPath)" -g "$(File)":$(Line))ARGS";
        constexpr std::string_view kVisualStudioArguments = R"ARGS(/edit "$(File)")ARGS";
        constexpr std::string_view kJetBrainsArguments = R"ARGS(--line $(Line) "$(File)")ARGS";
        constexpr std::string_view kPlainArguments = R"ARGS("$(File)")ARGS";

        std::string Lowercase(std::string text)
        {
            std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        }

        bool IsFile(const fs::path& path)
        {
            std::error_code error;
            return fs::is_regular_file(path, error);
        }

        void ReplaceAll(std::string& text, std::string_view from, const std::string& to)
        {
            for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
                text.replace(at, from.size(), to);
        }

        void AddIfFound(std::vector<CodeEditor>& editors, std::string name, const fs::path& program)
        {
            if (!IsFile(program))
                return;

            if (std::ranges::any_of(editors, [&](const CodeEditor& editor) { return editor.Program == program; }))
                return;

            editors.push_back({ std::move(name), program, DefaultScriptEditorArguments(program) });
        }
    }

    std::vector<CodeEditor> FindCodeEditors()
    {
        std::vector<CodeEditor> editors;
        for (const auto& [name, program] : platform::CodeEditorCandidates())
            AddIfFound(editors, name, program);

        return editors;
    }

    std::string DefaultScriptEditorArguments(const fs::path& program)
    {
        const std::string name = Lowercase(program.stem().string());

        if (name == "code" || name == "code - insiders" || name == "codium")
            return std::string(kVSCodeArguments);

        if (name == "devenv")
            return std::string(kVisualStudioArguments);

        if (name.starts_with("rider") || name.starts_with("clion") || name.starts_with("idea") || name.starts_with("pycharm"))
            return std::string(kJetBrainsArguments);

        return std::string(kPlainArguments);
    }

    std::vector<std::string> ExpandScriptEditorArguments(const std::string& arguments, const fs::path& file, int line, const fs::path& project)
    {
        std::vector<std::string> split;
        std::string current;
        bool inQuotes = false;
        bool started = false; // so "" is an argument of its own, even though it is empty

        for (const char c : arguments)
        {
            if (c == '"')
            {
                inQuotes = !inQuotes;
                started = true;
            }
            else if (!inQuotes && (c == ' ' || c == '\t'))
            {
                if (started)
                    split.push_back(std::move(current));

                current.clear();
                started = false;
            }
            else
            {
                current += c;
                started = true;
            }
        }

        if (started)
            split.push_back(std::move(current));

        // Filled in after splitting, so a space in a path never splits it
        const std::string filePath = PathToUtf8(file.lexically_normal().make_preferred());
        const std::string projectPath = PathToUtf8(project.lexically_normal().make_preferred());
        const std::string lineText = std::to_string(std::max(line, 1));

        for (std::string& argument : split)
        {
            ReplaceAll(argument, "$(File)", filePath);
            ReplaceAll(argument, "$(Line)", lineText);
            ReplaceAll(argument, "$(ProjectPath)", projectPath);
        }

        return split;
    }

    void OpenInScriptEditor(const EditorSettings::ScriptEditor& editor, const fs::path& file, const fs::path& project, int line)
    {
        if (!editor.Program.empty())
        {
            if (StartProgram(editor.Program, ExpandScriptEditorArguments(editor.Arguments, file, line, project)))
                return;

            LOG_ERROR("[Editor] The script editor didn't start, so {} opens in the system's program for it", file.filename().string());
        }

        OpenInDefaultApp(file);
    }
}
