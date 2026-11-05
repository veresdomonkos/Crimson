#include "crimson_editor/ui/scene_viewport_panel.hpp"

#include <filesystem>

#include <glm/gtc/type_ptr.hpp>

#include <crimson/scene/scene_renderer.hpp>
#include <crimson/scene/scene_serializer.hpp>

#include "crimson_editor/ui/asset_drag.hpp"
#include "crimson_editor/ui/icons.hpp"

namespace crimson::editor::ui
{
    // Where the mouse points on the ground, unless it points above the horizon
    static glm::vec3 GetDropPosition(const glm::vec3& origin, const glm::vec3& direction)
    {
        constexpr float kDistanceWithoutGround = 5.0f;

        const float toGround = direction.y < -0.0001f ? -origin.y / direction.y : -1.0f;
        return origin + direction * (toGround > 0.0f ? toGround : kDistanceWithoutGround);
    }

    void SceneViewportPanel::OnImageDrawn(const ImVec2& position, const ImVec2& size)
    {
        HandleAssetDrop(position, size);

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

        glm::vec3 direction;
        if (!GetRayUnderMouse(position, size, direction))
            return;

        Scene& scene = m_context.ActiveScene;
        Entity entity = scene.Raycast(m_camera.GetPosition(), direction);

        // What is inside an instance can't be changed here, so a click means the instance
        while (entity && entity.HasComponent<InstancedComponent>())
            entity = scene.GetEntity(entity.GetComponent<RelationshipComponent>().Parent);

        m_context.SelectEntity(entity);
    }

    bool SceneViewportPanel::GetRayUnderMouse(const ImVec2& position, const ImVec2& size, glm::vec3& direction) const
    {
        const ImGuiIO& io = ImGui::GetIO();
        const glm::vec2 uv((io.MousePos.x - position.x) / size.x, (io.MousePos.y - position.y) / size.y);

        // The bars around the image aren't part of the scene
        if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
            return false;

        const glm::mat4 clipToWorld = glm::inverse(m_camera.GetViewProj());

        // The panel shows the image flipped, so the top of it is the top of clip space
        const glm::vec4 farPoint = clipToWorld * glm::vec4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 1.0f, 1.0f);

        direction = glm::normalize(glm::vec3(farPoint) / farPoint.w - m_camera.GetPosition());
        return true;
    }

    void SceneViewportPanel::HandleAssetDrop(const ImVec2& position, const ImVec2& size)
    {
        const AssetID mesh = AcceptAssetOnLastItem(m_context.Assets, AssetType::Mesh);
        const AssetID material = AcceptAssetOnLastItem(m_context.Assets, AssetType::Material);
        const AssetID scene = AcceptAssetOnLastItem(m_context.Assets, AssetType::Scene);

        if (!mesh && !material && !scene)
            return;

        glm::vec3 direction;
        if (!GetRayUnderMouse(position, size, direction))
            return;

        const glm::vec3 origin = m_camera.GetPosition();

        m_context.Deferred.emplace_back([this, mesh, material, scene, origin, direction]
        {
            if (mesh)
                CreateEntityFromMesh(mesh, origin, direction);
            else if (scene)
                CreateInstanceOfScene(scene, origin, direction);
            else
                AssignMaterialToEntity(material, origin, direction);
        });
    }

    void SceneViewportPanel::CreateEntityFromMesh(AssetID mesh, const glm::vec3& origin, const glm::vec3& direction)
    {
        const AssetInfo* info = m_context.Assets.GetInfo(mesh);
        if (!info)
            return;

        const Entity entity = m_context.ActiveScene.CreateEntity(std::filesystem::path(info->Path).stem().string());
        entity.GetComponent<TransformComponent>().Position = GetDropPosition(origin, direction);

        auto& renderer = entity.AddComponent<MeshRendererComponent>();
        renderer.MeshAsset = mesh;
        ResolveMeshRenderer(renderer, m_context.Assets);

        m_context.SelectEntity(entity);
    }

    void SceneViewportPanel::CreateInstanceOfScene(AssetID scene, const glm::vec3& origin, const glm::vec3& direction)
    {
        const Entity entity = CreateSceneInstance(scene, m_context.ActiveScene, {}, m_context.Assets, m_context.ActiveSceneAsset);
        if (!entity)
            return;

        entity.GetComponent<TransformComponent>().Position = GetDropPosition(origin, direction);
        m_context.SelectEntity(entity);
    }

    void SceneViewportPanel::AssignMaterialToEntity(AssetID material, const glm::vec3& origin, const glm::vec3& direction)
    {
        const Entity entity = m_context.ActiveScene.Raycast(origin, direction);
        if (!entity || entity.HasComponent<InstancedComponent>())
            return;

        auto* renderer = entity.TryGetComponent<MeshRendererComponent>();
        if (!renderer || renderer->MaterialAssets.empty())
            return;

        renderer->MaterialAssets[0] = material;
        ResolveMeshRenderer(*renderer, m_context.Assets);
    }

    void SceneViewportPanel::HandleShortcuts()
    {
        const ImGuiIO& io = ImGui::GetIO();

        // While flying, the same keys move the camera, and with Ctrl they are other shortcuts
        if (io.WantTextInput || io.KeyCtrl || ImGui::IsMouseDown(ImGuiMouseButton_Right))
            return;

        if (IsFocused() || IsHovered())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_operation = ImGuizmo::TRANSLATE;
            if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_operation = ImGuizmo::ROTATE;
            if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_operation = ImGuizmo::SCALE;
        }

        // What is inside an instance can't be changed here
        const Entity selected = m_context.Selected;
        if (IsFocused() && selected && !selected.HasComponent<InstancedComponent>() && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
            m_context.Deferred.emplace_back([this, selected] { m_context.ActiveScene.DestroyEntity(selected); });
    }

    void SceneViewportPanel::DrawToolbar(const ImVec2& position)
    {
        constexpr float kMargin = 8.0f;

        ImGui::SetCursorScreenPos(ImVec2(position.x + kMargin, position.y + kMargin));

        const auto operationButton = [this](const char* icon, const char* name, const char* shortcut, ImGuizmo::OPERATION operation)
        {
            const bool active = m_operation == operation;

            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

            ImGui::PushID(name);
            if (ImGui::Button(icon))
                m_operation = operation;
            ImGui::PopID();

            ImGui::SetItemTooltip("%s (%s)", name, shortcut);

            if (active)
                ImGui::PopStyleColor();

            ImGui::SameLine();
        };

        operationButton(kIconMove, "Move", "W", ImGuizmo::TRANSLATE);
        operationButton(kIconRotate, "Rotate", "E", ImGuizmo::ROTATE);
        operationButton(kIconScale, "Scale", "R", ImGuizmo::SCALE);

        const bool local = m_mode == ImGuizmo::LOCAL;
        if (ImGui::Button(local ? kIconLocalSpace : kIconWorldSpace))
            m_mode = local ? ImGuizmo::WORLD : ImGuizmo::LOCAL;

        ImGui::SetItemTooltip(local ? "Local axes, click for world axes" : "World axes, click for local axes");
    }

    void SceneViewportPanel::DrawGizmo(const ImVec2& position, const ImVec2& size)
    {
        const Entity entity = m_context.Selected;
        if (!entity || entity.HasComponent<InstancedComponent>())
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
