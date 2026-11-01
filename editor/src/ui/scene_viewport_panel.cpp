#include "crimson_editor/ui/scene_viewport_panel.hpp"

#include <glm/gtc/type_ptr.hpp>

namespace crimson::editor::ui
{
    void SceneViewportPanel::OnImageDrawn(const ImVec2& position, const ImVec2& size)
    {
        // Left clicks focus a window by themselves, but flying starts with a right click
        if (IsHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            ImGui::SetWindowFocus();

        HandleShortcuts();
        DrawGizmo(position, size);
        DrawToolbar(position);
        HandlePicking(position, size);
    }

    void SceneViewportPanel::HandlePicking(const ImVec2& position, const ImVec2& size)
    {
        const ImGuiIO& io = ImGui::GetIO();

        if (!IsHovered() || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) || io.KeyAlt)
            return;

        // These clicks belong to the toolbar and the gizmo. A viewport image doesn't count as a hovered item.
        if (ImGui::IsAnyItemHovered() || (m_context.Selected && ImGuizmo::IsOver()))
            return;

        const glm::vec2 uv((io.MousePos.x - position.x) / size.x, (io.MousePos.y - position.y) / size.y);

        // The bars around the image aren't part of the scene
        if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
            return;

        const glm::mat4 clipToWorld = glm::inverse(m_camera.GetViewProj());

        // The panel shows the image flipped, so the top of it is the top of clip space
        const glm::vec4 farPoint = clipToWorld * glm::vec4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 1.0f, 1.0f);

        const glm::vec3 origin = m_camera.GetPosition();
        const glm::vec3 direction = glm::normalize(glm::vec3(farPoint) / farPoint.w - origin);

        m_context.Selected = m_context.ActiveScene.Raycast(origin, direction);
    }

    void SceneViewportPanel::HandleShortcuts()
    {
        if (ImGui::GetIO().WantTextInput)
            return;

        if (ImGui::IsKeyPressed(ImGuiKey_1, false)) m_operation = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_2, false)) m_operation = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_3, false)) m_operation = ImGuizmo::SCALE;
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

        // Holding alt is for moving the camera
        ImGuizmo::Enable(!ImGui::GetIO().KeyAlt);

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
