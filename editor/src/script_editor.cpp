#include "crimson_editor/script_editor.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <system_error>

#include <crimson/core/log.hpp>

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

        // An environment variable as a path, empty if it isn't set
        fs::path EnvironmentPath(const char* name)
        {
#ifdef _WIN32
            const std::wstring wideName(name, name + std::char_traits<char>::length(name));
            const wchar_t* value = _wgetenv(wideName.c_str());
            return value ? fs::path(value) : fs::path();
#else
            const char* value = std::getenv(name);
            return value ? fs::path(value) : fs::path();
#endif
        }

        void AddIfFound(std::vector<CodeEditor>& editors, std::string name, const fs::path& program)
        {
            if (!IsFile(program))
                return;

            if (std::ranges::any_of(editors, [&](const CodeEditor& editor) { return editor.Program == program; }))
                return;

            editors.push_back({ std::move(name), program, DefaultScriptEditorArguments(program) });
        }

#ifndef _WIN32
        // Where a program is on the PATH, empty if it isn't there
        fs::path FindOnPath(const std::string& program)
        {
            const std::string path = EnvironmentPath("PATH").string();

            for (size_t start = 0; start <= path.size();)
            {
                const size_t end = std::min(path.find(':', start), path.size());
                if (end > start)
                {
                    const fs::path candidate = fs::path(path.substr(start, end - start)) / program;
                    if (IsFile(candidate))
                        return candidate;
                }

                start = end + 1;
            }

            return {};
        }
#endif
    }

    std::vector<CodeEditor> FindCodeEditors()
    {
        std::vector<CodeEditor> editors;
        std::error_code error;

#ifdef _WIN32
        const fs::path localPrograms = EnvironmentPath("LOCALAPPDATA") / "Programs";
        const std::vector<fs::path> programFiles = { EnvironmentPath("ProgramFiles"), EnvironmentPath("ProgramFiles(x86)") };

        // Its command line, bin\code.cmd, is what takes -g file:line, so that comes before Code.exe itself
        for (const fs::path& install : { localPrograms / "Microsoft VS Code", programFiles[0] / "Microsoft VS Code", programFiles[1] / "Microsoft VS Code" })
        {
            const fs::path commandLine = install / "bin" / "code.cmd";
            AddIfFound(editors, "VS Code", IsFile(commandLine) ? commandLine : install / "Code.exe");
        }

        // Visual Studio is in a folder for each version, then one for each edition
        for (const fs::path& root : programFiles)
        {
            const fs::path studio = root / "Microsoft Visual Studio";

            for (fs::directory_iterator version(studio, error), end; !error && version != end; version.increment(error))
            {
                for (fs::directory_iterator edition(version->path(), error); !error && edition != end; edition.increment(error))
                {
                    AddIfFound(editors, "Visual Studio " + version->path().filename().string() + " " + edition->path().filename().string(),
                               edition->path() / "Common7" / "IDE" / "devenv.exe");
                }

                error.clear();
            }

            error.clear();
        }

        // JetBrains installs a folder for each version, and the Toolbox puts them in the user's programs
        for (const fs::path& root : { programFiles[0] / "JetBrains", localPrograms })
        {
            for (fs::directory_iterator product(root, error), end; !error && product != end; product.increment(error))
            {
                AddIfFound(editors, product->path().filename().string(), product->path() / "bin" / "rider64.exe");
                AddIfFound(editors, product->path().filename().string(), product->path() / "bin" / "clion64.exe");
            }

            error.clear();
        }
#else
        const std::pair<const char*, const char*> onPath[] = {
            { "VS Code", "code" }, { "VSCodium", "codium" }, { "Rider", "rider" }, { "CLion", "clion" }
        };

        for (const auto& [name, program] : onPath)
        {
            if (const fs::path found = FindOnPath(program); !found.empty())
                AddIfFound(editors, name, found);
        }

    #ifdef __APPLE__
        AddIfFound(editors, "VS Code", "/Applications/Visual Studio Code.app/Contents/Resources/app/bin/code");
    #endif
#endif

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
