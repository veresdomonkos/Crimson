#include "crimson_editor/ui/scene_viewport_panel.hpp"

#include <glm/gtc/type_ptr.hpp>

namespace crimson::editor::ui
{
    void SceneViewportPanel::OnImageDrawn(const ImVec2& position, const ImVec2& size)
    {
        HandleShortcuts();
        DrawGizmo(position, size);
        DrawToolbar(position);
    }

    void SceneViewportPanel::HandleShortcuts()
    {
        const ImGuiIO& io = ImGui::GetIO();

        // The right mouse button flies the camera with the same keys
        if (io.WantTextInput || ImGui::IsMouseDown(ImGuiMouseButton_Right))
            return;

        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_operation = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_operation = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_operation = ImGuizmo::SCALE;
    }

    void SceneViewportPanel::DrawToolbar(const ImVec2& position)
    {
        constexpr float kMargin = 8.0f;

        ImGui::SetCursorScreenPos(ImVec2(position.x + kMargin, position.y + kMargin));

        const auto operationButton = [this](const char* label, ImGuizmo::OPERATION operation)
        {
            const bool active = m_operation == operation;

            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

            if (ImGui::Button(label))
                m_operation = operation;

            if (active)
                ImGui::PopStyleColor();

            ImGui::SameLine();
        };

        operationButton("Move", ImGuizmo::TRANSLATE);
        operationButton("Rotate", ImGuizmo::ROTATE);
        operationButton("Scale", ImGuizmo::SCALE);

        if (ImGui::Button(m_mode == ImGuizmo::LOCAL ? "Local" : "World"))
            m_mode = m_mode == ImGuizmo::LOCAL ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
    }

    void SceneViewportPanel::DrawGizmo(const ImVec2& position, const ImVec2& size)
    {
        const Entity entity = m_context.Selected;
        if (!entity)
            return;

        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(position.x, position.y, size.x, size.y);

        const float translateSnap = 0.5f;
        const float rotateSnap = 15.0f;
        const float scaleSnap = 0.1f;

        const float snapValue = m_operation == ImGuizmo::TRANSLATE ? translateSnap
            : m_operation == ImGuizmo::ROTATE ? rotateSnap : scaleSnap;

        const glm::vec3 snap(snapValue);
        const bool snapping = ImGui::GetIO().KeyCtrl;

        Scene& scene = m_context.ActiveScene;

        const glm::mat4 view = m_camera.GetView();
        const glm::mat4 projection = m_camera.GetProjection();
        glm::mat4 world = scene.ComputeWorldMatrix(entity);

        ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), m_operation, m_mode,
            glm::value_ptr(world), nullptr, snapping ? glm::value_ptr(snap) : nullptr);

        if (ImGuizmo::IsUsing())
        {
            const Entity parent = scene.GetEntity(entity.GetComponent<RelationshipComponent>().Parent);
            entity.GetComponent<TransformComponent>().SetFromMatrix(glm::inverse(scene.ComputeWorldMatrix(parent)) * world);
        }
    }
}
