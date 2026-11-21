#include "crimson_editor/ui/window.hpp"

#include <string>

#include <imgui.h>

#include "crimson_editor/ui/editor_theme.hpp"
#include "imgui_convert.hpp"

namespace crimson::editor::ui
{
    WindowScope::WindowScope(std::string_view title, WindowKind kind)
    {
        ImGuiWindowFlags flags = ImGuiWindowFlags_None;

        if (kind == WindowKind::Fullscreen)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->Pos);
            ImGui::SetNextWindowSize(viewport->Size);

            // No saved settings, so that the editor's layout stays untouched
            flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking;
        }

        ImGui::Begin(std::string(title).c_str(), nullptr, flags);
    }

    WindowScope::~WindowScope()
    {
        ImGui::End();
    }

    OverlayWindow::OverlayWindow(std::string_view id, glm::vec2 anchor, glm::vec2 pivot, float alpha)
    {
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings
            | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove;

        ImGui::SetNextWindowPos(ToImVec2(anchor), ImGuiCond_Always, ToImVec2(pivot));
        ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ToImVec4(theme::PopupBackground));

        m_open = ImGui::Begin(std::string(id).c_str(), nullptr, flags);
    }

    OverlayWindow::~OverlayWindow()
    {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    bool OverlayWindow::Hovered() const
    {
        return ImGui::IsWindowHovered();
    }

    float OverlayWindow::Height() const
    {
        return ImGui::GetWindowHeight();
    }

    Rect WorkArea()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        return Rect{ FromImVec2(viewport->WorkPos), FromImVec2(viewport->WorkPos) + FromImVec2(viewport->WorkSize) };
    }
}
