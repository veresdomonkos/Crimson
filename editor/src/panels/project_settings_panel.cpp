#include "crimson_editor/panels/project_settings_panel.hpp"

#include "crimson_editor/panels/asset_field.hpp"
#include "crimson_editor/ui/layout.hpp"
#include "crimson_editor/ui/property_fields.hpp"
#include "crimson_editor/ui/widgets.hpp"

namespace crimson::editor
{
    void ProjectSettingsPanel::Draw()
    {
        ProjectSettings settings = m_context.Session.GetProjectSettings();
        bool changed = false;

        ui::SectionTitle("Game");

        if (ui::props::BeginTable("##ProjectSettings"))
        {
            ui::props::BeginRow("Main Scene");
            changed = AssetField("##MainScene", m_context, settings.MainScene);
            ui::Tooltip("The scene a built game starts with");
            ui::props::ResetTarget();

            if (ui::props::EndRow(bool(settings.MainScene)))
            {
                settings.MainScene = {};
                changed = true;
            }

            ui::props::EndTable();
        }

        if (changed)
            m_context.Session.SetProjectSettings(settings);
    }
}
