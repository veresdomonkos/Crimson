#include "crimson_editor/ui/inspector_panel.hpp"

#include <algorithm>
#include <string>

#include "crimson_editor/ui/asset_drag.hpp"
#include "crimson_editor/ui/component_fields.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/layout.hpp"
#include "crimson_editor/ui/menus.hpp"
#include "crimson_editor/ui/property_fields.hpp"
#include "crimson_editor/ui/widgets.hpp"

namespace crimson::editor::ui
{
    void InspectorPanel::Draw()
    {
        // Each selection's fields have ids of their own, so what the asset picker chose for one can't reach another's
        if (m_context.Session.GetSelectedAsset())
        {
            const ui::IdScope scope(m_context.Session.GetSelectedAsset().ToString());
            m_assetInspector.Draw(m_context.Session.GetSelectedAsset());
        }
        else if (const Entity entity = m_context.Session.GetSelected())
        {
            const ui::IdScope scope(entity.GetComponent<NameComponent>().Id.ToString());

            {
                // What comes from an instance's scene is only changed by opening that scene
                const ui::Disabled disabled(entity.HasComponent<InstancedComponent>());

                DrawHeader(entity);

                for (const ComponentType& type : m_context.Session.GetAssets().Components().Types())
                    DrawComponent(entity, type);

                DrawUnknownComponents(entity);

                // An instance's own components wouldn't be saved
                if (!entity.HasComponent<SceneInstanceComponent>())
                    DrawAddComponent(entity);
            }

            DrawSceneInstance(entity);
        }
        else
        {
            ui::EmptyHint("Select an entity in the Hierarchy or the Scene view, or an asset in the Assets panel");
        }
    }

    void InspectorPanel::DrawHeader(Entity entity)
    {
        auto& name = entity.GetComponent<NameComponent>();

        const ui::Row row;

        if (ui::Checkbox("##Active", name.Active))
            m_context.Session.MarkEdited();

        ui::NextItemFills();
        if (ui::InputText("##Name", name.Name))
            m_context.Session.MarkEdited();
    }

    void InspectorPanel::DrawComponent(Entity entity, const ComponentType& type)
    {
        void* component = type.Get(entity);
        if (!component)
            return;

        const char* icon = ComponentIcon(type, component);

        // What can be added can be removed, which every entity's own, like the transform, can't
        bool removed = false;

        {
            const ui::Section section(type.Name, icon ? icon : ui::icons::kEntity);

            if (type.Addable)
            {
                if (const ui::ContextMenu menu; menu && ui::MenuItem("Remove Component"))
                    removed = true;
            }

            if (section && ui::props::BeginTable(type.Id.c_str()))
            {
                if (DrawComponentFields(type, component, m_context))
                    m_context.Session.MarkEdited();

                ui::props::EndTable();
            }
        }

        if (removed)
        {
            type.Remove(entity);
            m_context.Session.MarkEdited();
        }
    }

    void InspectorPanel::DrawUnknownComponents(Entity entity)
    {
        const auto* unknown = entity.TryGetComponent<UnknownComponents>();
        if (!unknown)
            return;

        // Like one from a script that isn't loaded. It can't be shown, but the scene keeps it.
        for (const SceneComponent& component : unknown->Components)
        {
            ui::Text(std::string(ui::icons::kWarning) + "  " + component.Type, ui::TextStyle::Muted);
            ui::Tooltip("A component this editor doesn't know. It stays as it is when the scene is saved.");
        }
    }

    void InspectorPanel::DrawSceneInstance(Entity entity)
    {
        const auto* instance = entity.TryGetComponent<SceneInstanceComponent>();
        if (!instance)
            return;

        ui::Spacing();

        const std::string label = "Instance of " + AssetLabel(m_context.Session.GetAssets(), instance->Scene);

        if (m_context.Session.GetAssets().IsMissing(instance->Scene))
        {
            ui::Text(label, ui::theme::Warning);
            ui::Text("It shows again once the file is back and the scene is opened again", ui::TextStyle::Muted);
            return;
        }

        ui::Text(label, ui::TextStyle::Muted);

        if (ui::Button("Open Source", ui::ButtonSize::Fill))
            m_context.Session.OpenScene(instance->Scene);
    }

    void InspectorPanel::DrawAddComponent(Entity entity)
    {
        ui::Spacing();

        if (ui::Button("Add Component", ui::ButtonSize::Fill))
            ui::Popup::Open("AddComponent");

        if (const ui::Popup popup("AddComponent"); popup)
        {
            for (const ComponentType& type : m_context.Session.GetAssets().Components().Types())
            {
                if (!type.Addable)
                    continue;

                // Between frames, as adding a component can move the others, which this frame's panels still point to
                if (ui::MenuItem(type.Name, {}, !type.Has(entity)))
                {
                    m_context.Deferred.emplace_back([this, &type, entity]
                    {
                        if (!entity || type.Has(entity))
                            return;

                        type.Add(entity);
                    });
                }
            }
        }
    }
}
