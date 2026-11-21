#include "crimson_editor/ui/scene_viewport_panel.hpp"

#include <algorithm>
#include <filesystem>

#include <glm/gtc/type_ptr.hpp>

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

    // Whether dropping an asset of this type does anything: a scene is instanced, others need a component that makes an
    // entity from them or takes them
    static bool CanDrop(const ComponentRegistry& components, AssetType type)
    {
        if (type.Is<SceneAsset>() || FindSpawnField(components, type))
            return true;

        return std::ranges::any_of(components.Types(), [type](const ComponentType& component)
        {
            return std::ranges::any_of(component.Fields, [type](const FieldInfo& field) { return field.DropTarget && field.Asset == type; });
        });
    }

    SceneViewportPanel::SceneViewportPanel(TextureHandle texture, float aspect, EditorContext& context, SceneView& view)
        : ViewportPanel({ .Title = "Scene", .Icon = icons::kScene, .Slot = DockSlot::Center, .Closable = false, .FillsWindow = true }, texture, aspect),
          m_context(context), m_view(view)
    {
        // While flying, the same keys move the camera
        const auto notFlying = [] { return !ImGui::IsMouseDown(ImGuiMouseButton_Right); };

        AddCommand({ .Shortcuts = { { Key::W } }, .Enabled = notFlying, .Run = [this] { m_operation = ImGuizmo::TRANSLATE; } });
        AddCommand({ .Shortcuts = { { Key::E } }, .Enabled = notFlying, .Run = [this] { m_operation = ImGuizmo::ROTATE; } });
        AddCommand({ .Shortcuts = { { Key::R } }, .Enabled = notFlying, .Run = [this] { m_operation = ImGuizmo::SCALE; } });

        // What is inside an instance can't be changed here
        AddCommand({ .Shortcuts = { { Key::Delete } },
                     .Enabled = [this, notFlying]
                     {
                         const Entity selected = m_context.Session.GetSelected();
                         return notFlying() && selected && !selected.HasComponent<InstancedComponent>();
                     },
                     .Run = [this]
                     {
                         const Entity selected = m_context.Session.GetSelected();
                         m_context.Deferred.emplace_back([this, selected]
                         {
                             m_context.Session.GetSceneEdits().RecordDestroy(selected);
                             m_context.Session.GetScene().DestroyEntity(selected);
                         });
                     } });
    }

    void SceneViewportPanel::OnImageDrawn(const ImVec2& position, const ImVec2& size)
    {
        HandleAssetDrop(position, size);

        // Left clicks focus a window by themselves, but flying starts with a right click
        if (IsHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            Show();

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
        if (ImGui::IsAnyItemHovered() || (m_context.Session.GetSelected() && ImGuizmo::IsOver()))
            return;

        glm::vec3 origin;
        glm::vec3 direction;
        if (!GetRayUnderMouse(position, size, origin, direction))
            return;

        Scene& scene = m_context.Session.GetScene();
        Entity entity = scene.Raycast(origin, direction, m_context.Session.GetAssets());

        // What is inside an instance can't be changed here, so a click means the instance
        while (entity && entity.HasComponent<InstancedComponent>())
            entity = scene.GetEntity(entity.GetComponent<RelationshipComponent>().Parent);

        m_context.Session.SelectEntity(entity);
    }

    bool SceneViewportPanel::GetRayUnderMouse(const ImVec2& position, const ImVec2& size, glm::vec3& origin, glm::vec3& direction) const
    {
        const ImGuiIO& io = ImGui::GetIO();
        const glm::vec2 uv((io.MousePos.x - position.x) / size.x, (io.MousePos.y - position.y) / size.y);

        // The bars around the image aren't part of the scene
        if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
            return false;

        const CameraView view = m_view.GetView();
        const glm::mat4 clipToWorld = glm::inverse(view.GetViewProj());

        // The panel shows the image flipped, so the top of it is the top of clip space
        const glm::vec2 ndc(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
        const glm::vec4 nearPoint = clipToWorld * glm::vec4(ndc, 0.0f, 1.0f);
        const glm::vec4 farPoint = clipToWorld * glm::vec4(ndc, 1.0f, 1.0f);

        origin = glm::vec3(nearPoint) / nearPoint.w;
        direction = glm::normalize(glm::vec3(farPoint) / farPoint.w - origin);
        return true;
    }

    void SceneViewportPanel::HandleAssetDrop(const ImVec2& position, const ImVec2& size)
    {
        const ComponentRegistry& components = m_context.Session.GetAssets().Components();

        AssetID dropped;
        AssetType droppedType;

        for (const AssetType type : m_context.Session.GetAssets().Types())
        {
            if (!CanDrop(components, type))
                continue;

            if (const AssetID id = asset_drag::AcceptOnLastItem(m_context.Session.GetAssets(), type))
            {
                dropped = id;
                droppedType = type;
            }
        }

        if (!dropped)
            return;

        glm::vec3 origin;
        glm::vec3 direction;
        if (!GetRayUnderMouse(position, size, origin, direction))
            return;

        m_context.Deferred.emplace_back([this, dropped, droppedType, origin, direction]
        {
            if (droppedType.Is<SceneAsset>())
                CreateInstanceOfScene(m_context.Session.GetAssets().As<SceneAsset>(dropped), origin, direction);
            else if (const FieldRef spawn = FindSpawnField(m_context.Session.GetAssets().Components(), droppedType))
                CreateEntityFromAsset(spawn, dropped, origin, direction);
            else
                AssignAssetToEntity(dropped, droppedType, origin, direction);
        });
    }

    void SceneViewportPanel::CreateEntityFromAsset(FieldRef spawn, AssetID asset, const glm::vec3& origin, const glm::vec3& direction)
    {
        const AssetInfo* info = m_context.Session.GetAssets().GetInfo(asset);
        if (!info)
            return;

        const Entity entity = m_context.Session.GetScene().CreateEntity(std::filesystem::path(info->Path).stem().string());
        entity.GetComponent<TransformComponent>().Position = GetDropPosition(origin, direction);

        spawn.Type->Add(entity);
        AssignAsset(*spawn.Type, spawn.Field, entity, asset, m_context.Session.GetAssets());

        m_context.Session.GetSceneEdits().RecordCreated(entity);
        m_context.Session.SelectEntity(entity);
    }

    void SceneViewportPanel::CreateInstanceOfScene(SceneID scene, const glm::vec3& origin, const glm::vec3& direction)
    {
        const Entity entity = CreateSceneInstance(scene, m_context.Session.GetScene(), {}, m_context.Session.GetAssets(), m_context.Session.GetSceneAsset());
        if (!entity)
            return;

        entity.GetComponent<TransformComponent>().Position = GetDropPosition(origin, direction);

        m_context.Session.GetSceneEdits().RecordCreated(entity);
        m_context.Session.SelectEntity(entity);
    }

    void SceneViewportPanel::AssignAssetToEntity(AssetID asset, AssetType type, const glm::vec3& origin, const glm::vec3& direction)
    {
        const Entity entity = m_context.Session.GetScene().Raycast(origin, direction, m_context.Session.GetAssets());
        if (!entity || entity.HasComponent<InstancedComponent>())
            return;

        if (const FieldRef target = FindDropField(m_context.Session.GetAssets().Components(), entity, type))
        {
            m_context.Session.GetSceneEdits().Record(entity);
            AssignAsset(*target.Type, target.Field, entity, asset, m_context.Session.GetAssets());
        }
    }

    void SceneViewportPanel::DrawToolbar(const ImVec2& position)
    {
        const float margin = theme::Scaled(theme::SpaceS);

        ImGui::SetCursorScreenPos(ImVec2(position.x + margin, position.y + margin));

        const auto operationButton = [this](const char* icon, const char* name, const char* shortcut, ImGuizmo::OPERATION operation)
        {
            const bool active = m_operation == operation;

            ImGui::PushID(name);
            if (theme::ToggleButton(icon, active))
                m_operation = operation;
            ImGui::PopID();

            ImGui::SetItemTooltip("%s (%s)", name, shortcut);

            ImGui::SameLine();
        };

        operationButton(icons::kMove, "Move", "W", ImGuizmo::TRANSLATE);
        operationButton(icons::kRotate, "Rotate", "E", ImGuizmo::ROTATE);
        operationButton(icons::kScale, "Scale", "R", ImGuizmo::SCALE);

        const bool local = m_mode == ImGuizmo::LOCAL;
        if (ImGui::Button(local ? icons::kLocalSpace : icons::kWorldSpace))
            m_mode = local ? ImGuizmo::WORLD : ImGuizmo::LOCAL;

        ImGui::SetItemTooltip(local ? "Local axes, click for world axes" : "World axes, click for local axes");

        ImGui::SameLine();

        const Entity selected = m_context.Session.GetSelected();
        const Entity looked = m_view.GetLookThrough();
        const bool looking = m_view.IsLookingThrough();

        ImGui::BeginDisabled(!looking && !(selected && selected.HasComponent<CameraComponent>()));

        ImGui::PushID("LookThrough");
        if (theme::ToggleButton(icons::kCamera, looking))
        {
            if (looking)
                m_view.StopLookingThrough();
            else
                m_view.LookThrough(selected);
        }
        ImGui::PopID();

        ImGui::EndDisabled();

        if (looking)
            ImGui::SetItemTooltip("Stop looking through %s", looked.GetComponent<NameComponent>().Name.c_str());
        else
            ImGui::SetItemTooltip("Look through the selected camera");
    }

    void SceneViewportPanel::DrawGizmo(const ImVec2& position, const ImVec2& size)
    {
        const Entity entity = m_context.Session.GetSelected();
        if (!entity || entity.HasComponent<InstancedComponent>())
            return;

        // The gizmo would be at the eye, and dragging it would move the view under the drag
        if (entity == m_view.GetLookThrough())
            return;

        // Holding alt is for moving the camera
        ImGuizmo::Enable(!ImGui::GetIO().KeyAlt);

        // The handles always point along the positive axes, as in Unity, rather than turning to face the camera
        ImGuizmo::AllowAxisFlip(false);

        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(position.x, position.y, size.x, size.y);

        const float translateSnap = 0.5f;
        const float rotateSnap = 15.0f;
        const float scaleSnap = 0.1f;

        const float snapValue = m_operation == ImGuizmo::TRANSLATE ? translateSnap
            : m_operation == ImGuizmo::ROTATE ? rotateSnap : scaleSnap;

        const glm::vec3 snap(snapValue);
        const bool snapping = ImGui::GetIO().KeyCtrl;

        Scene& scene = m_context.Session.GetScene();

        const CameraView view = m_view.GetView();
        glm::mat4 world = scene.ComputeWorldMatrix(entity);

        // Only an orthographic projection has a 1 there
        ImGuizmo::SetOrthographic(view.Projection[3][3] == 1.0f);

        ImGuizmo::Manipulate(glm::value_ptr(view.View), glm::value_ptr(view.Projection), m_operation, m_mode,
            glm::value_ptr(world), nullptr, snapping ? glm::value_ptr(snap) : nullptr);

        m_context.GizmoInUse = ImGuizmo::IsUsing();

        if (m_context.GizmoInUse)
        {
            const Entity parent = scene.GetEntity(entity.GetComponent<RelationshipComponent>().Parent);
            entity.GetComponent<TransformComponent>().SetFromMatrix(glm::inverse(scene.ComputeWorldMatrix(parent)) * world);
        }
    }
}
