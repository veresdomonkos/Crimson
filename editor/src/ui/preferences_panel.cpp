#include "crimson_editor/ui/preferences_panel.hpp"

#include <algorithm>
#include <optional>
#include <string>

#include <crimson/core/window.hpp>

#include "crimson_editor/file_dialog.hpp"
#include "crimson_editor/system_shell.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/input.hpp"
#include "crimson_editor/ui/layout.hpp"
#include "crimson_editor/ui/property_fields.hpp"
#include "crimson_editor/ui/widgets.hpp"

namespace fs = std::filesystem;

namespace crimson::editor::ui
{
    constexpr const char* kSystemDefault = "System default";

    void PreferencesPanel::OnOpened()
    {
        m_found = script_editor::FindCodeEditors();
    }

    void PreferencesPanel::Draw()
    {
        ui::SectionTitle("External Tools");
        DrawScriptEditor();
    }

    void PreferencesPanel::DrawScriptEditor()
    {
        const EditorSettings::ScriptEditor current = m_settings.GetScriptEditor();

        // A program that was found shows by its name, any other one by its file's
        std::string currentName = kSystemDefault;
        if (!current.Program.empty())
        {
            const auto found = std::ranges::find(m_found, current.Program, &CodeEditor::Program);
            currentName = found != m_found.end() ? found->Name : current.Program.filename().string();
        }

        bool browse = false;

        if (ui::props::BeginTable("##ExternalTools"))
        {
            ui::props::BeginRow("Script Editor");

            {
                const ui::Combo combo("##ScriptEditor", currentName);

                if (combo)
                {
                    if (ui::Selectable(kSystemDefault, current.Program.empty()))
                        m_settings.SetScriptEditor({});

                    for (const CodeEditor& editor : m_found)
                    {
                        const ui::IdScope scope(PathToUtf8(editor.Program));
                        if (ui::Selectable(editor.Name, editor.Program == current.Program))
                            m_settings.SetScriptEditor({ .Program = editor.Program, .Arguments = editor.Arguments });

                        ui::Tooltip(PathToUtf8(editor.Program));
                    }

                    ui::Separator();
                    browse = ui::Selectable("Browse...", false);
                }
            }

            ui::Tooltip(std::string("What shaders open in, and code files later. ") + kSystemDefault + " is the program the system has for the file's type.");
            ui::props::ResetTarget();

            if (ui::props::EndRow(!current.Program.empty()))
                m_settings.SetScriptEditor({});

            if (!current.Program.empty())
            {
                const std::string program = PathToUtf8(current.Program);
                ui::props::Text("Program", program.c_str());
                ui::Tooltip(program);

                // What is being typed stays as it is until it's done, then it is saved
                if (!m_editingArguments)
                    m_arguments = current.Arguments;

                ui::props::BeginRow("Arguments");
                ui::InputText("##Arguments", m_arguments);
                m_editingArguments = ui::item::Active();

                if (ui::item::DeactivatedAfterEdit())
                    m_settings.SetScriptEditor({ .Program = current.Program, .Arguments = m_arguments });

                ui::props::ResetTarget();

                if (ui::props::EndRow(true))
                    m_settings.SetScriptEditor({ .Program = current.Program, .Arguments = script_editor::DefaultArguments(current.Program) });
            }

            ui::props::EndTable();
        }

        if (!current.Program.empty())
        {
            ui::WrappedText("$(File) is the file, $(Line) the line it opens at, and $(ProjectPath) the project's folder.", ui::theme::TextMuted);
        }

        if (m_found.empty())
        {
            ui::WrappedText("No code editor was found where they are usually installed. Browse... picks any program.", ui::theme::TextMuted);
        }

        // After the combo has closed, as the picker blocks until it does
        if (browse)
        {
            const fs::path start = current.Program.empty() ? fs::path() : current.Program.parent_path();

            if (const std::optional<fs::path> picked = PickProgram(m_window, start))
                m_settings.SetScriptEditor({ .Program = *picked, .Arguments = script_editor::DefaultArguments(*picked) });
        }
    }
}
