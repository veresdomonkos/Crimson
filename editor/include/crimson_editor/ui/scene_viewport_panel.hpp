#pragma once
#include <imgui.h>
#include <ImGuizmo.h>

#include "crimson/renderer/camera_data.hpp"
#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/scene_view.hpp"
#include "crimson_editor/ui/viewport_panel.hpp"

namespace crimson::editor::ui
{
    // The scene's view, with a gizmo on the selected entity
    class SceneViewportPanel : public ViewportPanel
    {
    public:
        SceneViewportPanel(std::string_view name, ImTextureID texture, float aspect, EditorContext& context, SceneView& view)
            : ViewportPanel(name, texture, aspect), m_context(context), m_view(view) {}
    protected:
        void OnImageDrawn(const ImVec2& position, const ImVec2& size) override;
    private:
        void HandlePicking(const ImVec2& position, const ImVec2& size);
        void HandleAssetDrop(const ImVec2& position, const ImVec2& size);
        void CreateEntityFromAsset(FieldRef spawn, AssetID asset, const glm::vec3& origin, const glm::vec3& direction);
        void PlaceDropped(Entity entity, const glm::vec3& origin, const glm::vec3& direction);
        void AssignAssetToEntity(AssetID asset, AssetType type, const glm::vec3& origin, const glm::vec3& direction);

        // False when the mouse is on the bars around the image. The ray starts on the near plane, as an orthographic view
        // has no eye for it to start from.
        bool GetRayUnderMouse(const ImVec2& position, const ImVec2& size, glm::vec3& origin, glm::vec3& direction) const;
        void HandleShortcuts();
        void DrawToolbar(const ImVec2& position);
        void DrawGizmo(const ImVec2& position, const ImVec2& size);
    private:
        EditorContext& m_context;
        SceneView& m_view;

        ImGuizmo::OPERATION m_operation = ImGuizmo::TRANSLATE;
        ImGuizmo::MODE m_mode = ImGuizmo::LOCAL;
    };
}
