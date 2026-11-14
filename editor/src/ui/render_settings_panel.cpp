#include "crimson_editor/ui/render_settings_panel.hpp"

#include <array>

#include <imgui.h>
#include <imgui_internal.h>

#include "crimson_editor/ui/linear_color.hpp"

namespace crimson::editor::ui
{
    void RenderSettingsPanel::OnImGui()
    {
        // A layout saved before this panel existed doesn't know about it, so it joins its neighbor
        if (const ImGuiWindow* stats = ImGui::FindWindowByName("Stats"); stats && stats->DockId != 0)
            ImGui::SetNextWindowDockID(stats->DockId, ImGuiCond_FirstUseEver);

        ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 15.0f, 0.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Render Settings");

        constexpr std::array kToneMapperNames = { "ACES", "None" };

        bool changed = ImGui::SliderFloat("Exposure", &m_environment.Exposure, 0.05f, 16.0f, "%.2f", ImGuiSliderFlags_Logarithmic);

        int toneMapper = static_cast<int>(m_environment.Mapper);
        if (ImGui::Combo("Tone Mapper", &toneMapper, kToneMapperNames.data(), static_cast<int>(kToneMapperNames.size())))
        {
            m_environment.Mapper = static_cast<ToneMapper>(toneMapper);
            changed = true;
        }

        if (changed)
        {
            m_context.Deferred.emplace_back([this, exposure = m_environment.Exposure, mapper = m_environment.Mapper]
            {
                m_renderer.SetToneMapping(exposure, mapper);
            });
        }

        LinearColorEdit("Ambient", &m_environment.Ambient.x, 3);

        ImGui::End();
    }
}
