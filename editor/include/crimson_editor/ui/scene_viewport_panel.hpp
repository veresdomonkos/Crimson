#pragma once
#include <imgui.h>
#include <ImGuizmo.h>

#include "crimson/renderer/camera_data.hpp"
#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/ui/viewport_panel.hpp"

namespace crimson::editor::ui
{
    // The scene's view, with a gizmo on the selected entity
    class SceneViewportPanel : public ViewportPanel
    {
    public:
        SceneViewportPanel(std::string_view name, ImTextureID texture, float aspect, EditorContext& context, const PerspectiveCamera& camera)
            : ViewportPanel(name, texture, aspect), m_context(context), m_camera(camera) {}
    protected:
        void OnImageDrawn(const ImVec2& position, const ImVec2& size) override;
    private:
        void HandlePicking(const ImVec2& position, const ImVec2& size);
        void HandleShortcuts();
        void DrawToolbar(const ImVec2& position);
        void DrawGizmo(const ImVec2& position, const ImVec2& size);
    private:
        EditorContext& m_context;
        const PerspectiveCamera& m_camera;

        ImGuizmo::OPERATION m_operation = ImGuizmo::TRANSLATE;
        ImGuizmo::MODE m_mode = ImGuizmo::LOCAL;
    };
}
