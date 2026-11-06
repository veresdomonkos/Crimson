#include "crimson_editor/ui/preferences_panel.hpp"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>

#include <imgui.h>

#include <crimson/core/window.hpp>

#include "crimson_editor/file_dialog.hpp"
#include "crimson_editor/system_shell.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/property_fields.hpp"

namespace fs = std::filesystem;

namespace crimson::editor::ui
{
    static constexpr const char* kSystemDefault = "System default";

    void PreferencesPanel::OnMainMenuBar()
    {
        // Adds to the Edit menu the Hierarchy started
        if (ImGui::BeginMenu("Edit"))
        {
            ImGui::Separator();

            if (ImGui::MenuItem("Preferences..."))
            {
                m_open = true;
                m_found = FindCodeEditors();
                ImGui::SetWindowFocus("Preferences");
            }

            ImGui::EndMenu();
        }
    }

    void PreferencesPanel::OnImGui()
    {
        if (!m_open)
            return;

        const float fontSize = ImGui::GetFontSize();
        ImGui::SetNextWindowSize(ImVec2(fontSize * 36.0f, fontSize * 14.0f), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Preferences", &m_open, ImGuiWindowFlags_NoCollapse))
        {
            theme::SectionTitle("External Tools");
            DrawScriptEditor();
        }

        ImGui::End();
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

        if (props::BeginTable("##ExternalTools"))
        {
            props::BeginRow("Script Editor");

            if (ImGui::BeginCombo("##ScriptEditor", currentName.c_str()))
            {
                if (ImGui::Selectable(kSystemDefault, current.Program.empty()))
                    m_settings.SetScriptEditor({});

                for (const CodeEditor& editor : m_found)
                {
                    ImGui::PushID(PathToUtf8(editor.Program).c_str());
                    if (ImGui::Selectable(editor.Name.c_str(), editor.Program == current.Program))
                        m_settings.SetScriptEditor({ .Program = editor.Program, .Arguments = editor.Arguments });

                    ImGui::SetItemTooltip("%s", PathToUtf8(editor.Program).c_str());
                    ImGui::PopID();
                }

                ImGui::Separator();
                browse = ImGui::Selectable("Browse...");

                ImGui::EndCombo();
            }

            ImGui::SetItemTooltip("What shaders open in, and code files later. %s is the program the system has for the file's type.", kSystemDefault);
            props::ResetTarget();

            if (props::EndRow(!current.Program.empty()))
                m_settings.SetScriptEditor({});

            if (!current.Program.empty())
            {
                const std::string program = PathToUtf8(current.Program);
                props::Text("Program", program.c_str());
                ImGui::SetItemTooltip("%s", program.c_str());

                // What is being typed stays as it is until it's done, then it is saved
                if (!m_editingArguments)
                    std::snprintf(m_arguments.data(), m_arguments.size(), "%s", current.Arguments.c_str());

                props::BeginRow("Arguments");
                ImGui::InputText("##Arguments", m_arguments.data(), m_arguments.size());
                m_editingArguments = ImGui::IsItemActive();

                if (ImGui::IsItemDeactivatedAfterEdit())
                    m_settings.SetScriptEditor({ .Program = current.Program, .Arguments = m_arguments.data() });

                props::ResetTarget();

                if (props::EndRow(true))
                    m_settings.SetScriptEditor({ .Program = current.Program, .Arguments = DefaultScriptEditorArguments(current.Program) });
            }

            props::EndTable();
        }

        if (!current.Program.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
            ImGui::TextWrapped("$(File) is the file, $(Line) the line it opens at, and $(ProjectPath) the project's folder.");
            ImGui::PopStyleColor();
        }

        if (m_found.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
            ImGui::TextWrapped("No code editor was found where they are usually installed. Browse... picks any program.");
            ImGui::PopStyleColor();
        }

        // After the combo has closed, as the picker blocks until it does
        if (browse)
        {
            const fs::path start = current.Program.empty() ? fs::path() : current.Program.parent_path();

            if (const std::optional<fs::path> picked = PickProgram(m_window, start))
                m_settings.SetScriptEditor({ .Program = *picked, .Arguments = DefaultScriptEditorArguments(*picked) });
        }
    }
}
