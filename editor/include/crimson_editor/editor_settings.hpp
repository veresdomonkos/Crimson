#pragma once
#include <filesystem>
#include <string>

namespace crimson::editor
{
    // What a user sets for the editor, for every project. Kept in settings.json in the editor's user folder, and
    // saved on every change.
    class EditorSettings
    {
    public:
        // The program code files open in. No program means the one the system has for the file's type.
        struct ScriptEditor
        {
            std::filesystem::path Program;
            std::string Arguments; // with $(File), $(Line) and $(ProjectPath) filled in when it starts

            bool operator==(const ScriptEditor&) const = default;
        };

        explicit EditorSettings(std::filesystem::path file);

        [[nodiscard]] const ScriptEditor& GetScriptEditor() const { return m_scriptEditor; }
        void SetScriptEditor(ScriptEditor editor);

        // Whether the Assets panel shows the engine's own assets, apart from the project's
        [[nodiscard]] bool GetShowEngineContent() const { return m_showEngineContent; }
        void SetShowEngineContent(bool show);
    private:
        void Load();
        void Save() const;
    private:
        std::filesystem::path m_file;
        ScriptEditor m_scriptEditor;
        bool m_showEngineContent = false;
    };
}
