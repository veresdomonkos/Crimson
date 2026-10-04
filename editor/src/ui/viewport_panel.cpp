// viewport_panel.cpp
#include "editor/ui/viewport_panel.hpp"

namespace crimson::editor::ui
{
    void ViewportPanel::OnImGui()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin(m_name.c_str(), nullptr,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();

        m_hovered = ImGui::IsWindowHovered();
        m_focused = ImGui::IsWindowFocused();

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 avail  = ImGui::GetContentRegionAvail();
        m_contentSize = avail;

        if (avail.x > 1.0f && avail.y > 1.0f)
        {
            ImGui::GetWindowDrawList()->AddRectFilled(
                origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(0, 0, 0, 255));

            ImVec2 size = (avail.x / avail.y > m_aspect)
                ? ImVec2(avail.y * m_aspect, avail.y)
                : ImVec2(avail.x, avail.x / m_aspect);

            ImGui::SetCursorScreenPos(ImVec2(
                origin.x + (avail.x - size.x) * 0.5f,
                origin.y + (avail.y - size.y) * 0.5f));

            ImGui::Image(m_texture, size, ImVec2(0, 1), ImVec2(1, 0));
        }

        ImGui::End();
    }
}