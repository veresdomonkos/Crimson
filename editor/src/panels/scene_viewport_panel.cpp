#include "crimson_editor/panels/scene_viewport_panel.hpp"

#include <algorithm>
#include <filesystem>

#include <crimson/scene/scene_serializer.hpp>

#include <format>
#include <optional>

#include "crimson_editor/panels/asset_drag.hpp"
#include "crimson_editor/ui/gizmo.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/input.hpp"
#include "crimson_editor/ui/layout.hpp"
#include "crimson_editor/ui/widgets.hpp"

namespace crimson::editor
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
        : ViewportPanel({ .Title = "Scene", .Icon = ui::icons::kScene, .Slot = ui::DockSlot::Center, .Closable = false, .FillsWindow = true }, texture, aspect),
          m_context(context), m_view(view)
    {
        // While flying, the same keys move the camera
        const auto notFlying = [] { return !ui::input::MouseDown(MouseButton::Right); };

        AddCommand({ .Shortcuts = { { Key::W } }, .Enabled = notFlying, .Run = [this] { m_tool = ui::GizmoTool::Move; } });
        AddCommand({ .Shortcuts = { { Key::E } }, .Enabled = notFlying, .Run = [this] { m_tool = ui::GizmoTool::Rotate; } });
        AddCommand({ .Shortcuts = { { Key::R } }, .Enabled = notFlying, .Run = [this] { m_tool = ui::GizmoTool::Scale; } });

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

    void SceneViewportPanel::OnImageDrawn(glm::vec2 position, glm::vec2 size)
    {
        HandleAssetDrop(position, size);

        // Left clicks focus a window by themselves, but flying starts with a right click
        if (IsHovered() && ui::input::MouseClicked(MouseButton::Right))
            Show();

        DrawGizmo(position, size);
        DrawToolbar(position);
        HandlePicking(position, size);
    }

    void SceneViewportPanel::HandlePicking(glm::vec2 position, glm::vec2 size)
    {
        if (!IsHovered() || !ui::input::MouseClicked(MouseButton::Left) || HasModifier(ui::input::Modifiers(), KeyModifiers::Alt))
            return;

        // These clicks belong to the toolbar and the gizmo. A viewport image doesn't count as a hovered item.
        if (ui::input::IsAnyItemHovered() || (m_context.Session.GetSelected() && ui::IsOverGizmo()))
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

    bool SceneViewportPanel::GetRayUnderMouse(glm::vec2 position, glm::vec2 size, glm::vec3& origin, glm::vec3& direction) const
    {
        const glm::vec2 uv = (ui::input::MousePosition() - position) / size;

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

    void SceneViewportPanel::HandleAssetDrop(glm::vec2 position, glm::vec2 size)
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

    void SceneViewportPanel::DrawToolbar(glm::vec2 position)
    {
        const float margin = ui::theme::Scaled(ui::theme::SpaceS);

        ui::SetCursor(position + glm::vec2(margin));

        ui::Row row;

        const auto toolButton = [this](const char* icon, const char* name, const char* shortcut, ui::GizmoTool tool)
        {
            const ui::IdScope scope(name);
            if (ui::ToggleButton(icon, m_tool == tool))
                m_tool = tool;

            ui::Tooltip(std::format("{} ({})", name, shortcut));
        };

        toolButton(ui::icons::kMove, "Move", "W", ui::GizmoTool::Move);
        toolButton(ui::icons::kRotate, "Rotate", "E", ui::GizmoTool::Rotate);
        toolButton(ui::icons::kScale, "Scale", "R", ui::GizmoTool::Scale);

        const bool local = m_space == ui::GizmoSpace::Local;
        if (ui::Button(local ? ui::icons::kLocalSpace : ui::icons::kWorldSpace))
            m_space = local ? ui::GizmoSpace::World : ui::GizmoSpace::Local;

        ui::Tooltip(local ? "Local axes, click for world axes" : "World axes, click for local axes");

        const Entity selected = m_context.Session.GetSelected();
        const Entity looked = m_view.GetLookThrough();
        const bool looking = m_view.IsLookingThrough();

        {
            const ui::Disabled disabled(!looking && !(selected && selected.HasComponent<CameraComponent>()));

            const ui::IdScope scope("LookThrough");
            if (ui::ToggleButton(ui::icons::kCamera, looking))
            {
                if (looking)
                    m_view.StopLookingThrough();
                else
                    m_view.LookThrough(selected);
            }
        }

        if (looking)
            ui::Tooltip("Stop looking through " + looked.GetComponent<NameComponent>().Name);
        else
            ui::Tooltip("Look through the selected camera");
    }

    void SceneViewportPanel::DrawGizmo(glm::vec2 position, glm::vec2 size)
    {
        const Entity entity = m_context.Session.GetSelected();
        if (!entity || entity.HasComponent<InstancedComponent>())
            return;

        // The gizmo would be at the eye, and dragging it would move the view under the drag
        if (entity == m_view.GetLookThrough())
            return;

        constexpr float translateSnap = 0.5f;
        constexpr float rotateSnap = 15.0f;
        constexpr float scaleSnap = 0.1f;

        const float snapValue = m_tool == ui::GizmoTool::Move ? translateSnap
            : m_tool == ui::GizmoTool::Rotate ? rotateSnap : scaleSnap;

        const KeyModifiers modifiers = ui::input::Modifiers();

        Scene& scene = m_context.Session.GetScene();

        const CameraView view = m_view.GetView();
        glm::mat4 world = scene.ComputeWorldMatrix(entity);

        m_context.GizmoInUse = ui::Gizmo(ui::Rect{ position, position + size }, view.View, view.Projection, world, ui::GizmoOptions{
            .Tool = m_tool,
            .Space = m_space,
            // Holding alt is for moving the camera
            .Enabled = !HasModifier(modifiers, KeyModifiers::Alt),
            .Snap = HasModifier(modifiers, KeyModifiers::Control) ? std::optional<float>(snapValue) : std::nullopt
        });

        if (m_context.GizmoInUse)
        {
            const Entity parent = scene.GetEntity(entity.GetComponent<RelationshipComponent>().Parent);
            entity.GetComponent<TransformComponent>().SetFromMatrix(glm::inverse(scene.ComputeWorldMatrix(parent)) * world);
        }
    }
}
