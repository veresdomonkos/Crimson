#include "crimson_editor/editor_settings.hpp"

#include <fstream>
#include <utility>

#include <crimson/core/log.hpp>
#include <nlohmann/json.hpp>

namespace crimson::editor
{
    EditorSettings::EditorSettings(std::filesystem::path file)
        : m_file(std::move(file))
    {
        Load();
    }

    void EditorSettings::SetScriptEditor(ScriptEditor editor)
    {
        if (editor == m_scriptEditor)
            return;

        m_scriptEditor = std::move(editor);
        Save();
    }

    void EditorSettings::Load()
    {
        std::ifstream file(m_file);
        if (!file)
            return; // a first start, with everything at its default

        try
        {
            const nlohmann::json json = nlohmann::json::parse(file);

            if (const auto editor = json.find("scriptEditor"); editor != json.end())
            {
                // Paths are written as UTF-8, so a program under a folder with any name comes back the same
                const std::string program = editor->value("program", std::string());
                m_scriptEditor.Program = std::filesystem::path(std::u8string(program.begin(), program.end()));
                m_scriptEditor.Arguments = editor->value("arguments", std::string());
            }
        }
        catch (const std::exception& e)
        {
            LOG_WARN("[Editor] Ignoring the settings in {}: {}", m_file.string(), e.what());
            m_scriptEditor = {};
        }
    }

    void EditorSettings::Save() const
    {
        const std::u8string program = m_scriptEditor.Program.u8string();

        const nlohmann::json json = {
            { "scriptEditor", { { "program", std::string(program.begin(), program.end()) }, { "arguments", m_scriptEditor.Arguments } } }
        };

        std::ofstream file(m_file);
        file << json.dump(2) << '\n';
        file.close();

        if (!file)
            LOG_WARN("[Editor] Can't save the settings to {}", m_file.string());
    }
}
