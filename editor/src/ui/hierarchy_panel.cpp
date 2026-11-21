#include "crimson_editor/ui/hierarchy_panel.hpp"

#include <algorithm>
#include <filesystem>
#include <string>

#include "crimson_editor/ui/component_fields.hpp"
#include "crimson_editor/ui/editor_theme.hpp"
#include "crimson_editor/ui/drag_drop.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/input.hpp"
#include "crimson_editor/ui/layout.hpp"
#include "crimson_editor/ui/menus.hpp"
#include "crimson_editor/ui/widgets.hpp"

namespace crimson::editor::ui
{
    constexpr const char* kEntityPayload = "CRIMSON_ENTITY";

    static constexpr Shortcut kRename{ Key::F2 };
    static constexpr Shortcut kDelete{ Key::Delete };

    // Where the commands sit in the main menu: Edit after Undo and Redo, and Create after it
    constexpr int kRenamePriority = 120;
    constexpr int kDeletePriority = 121;
    constexpr int kCreateEmptyPriority = 200;
    constexpr int kCreatePresetPriority = 210;

    // What kind of entity it is, at a glance
    static const char* EntityIcon(Entity entity, const ComponentRegistry& components)
    {
        if (entity.HasComponent<SceneInstanceComponent>())
            return ui::icons::kSceneInstance;

        // The first component with an icon, in the order they were registered, which puts a light before a mesh
        for (const ComponentType& type : components.Types())
        {
            if (type.InlineInFile)
                continue;

            if (const void* component = type.Get(entity))
            {
                if (const char* icon = ComponentIcon(type, component))
                    return icon;
            }
        }

        return ui::icons::kEntity;
    }

    HierarchyPanel::HierarchyPanel(EditorContext& context)
        : Panel({ .Title = "Hierarchy", .Icon = ui::icons::kEntity, .Slot = DockSlot::Left }),
          m_context(context)
    {
        AddCommand({ .MenuPath = "Edit/Rename", .Priority = kRenamePriority, .Shortcuts = { kRename },
                     .Enabled = [this] { return IsSelectionEditable(); },
                     .Run = [this] { BeginRename(m_context.Session.GetSelected()); } });

        AddCommand({ .MenuPath = "Edit/Delete", .Priority = kDeletePriority, .Shortcuts = { kDelete },
                     .Enabled = [this] { return IsSelectionEditable(); },
                     .Run = [this] { DeleteEntity(m_context.Session.GetSelected()); } });

        const std::vector<CreateOption> options = ListCreateOptions();

        for (size_t i = 0; i < options.size(); ++i)
        {
            const CreateOption option = options[i];
            const std::string path = option.Menu.empty() ? "Create/" + option.Name : "Create/Create " + option.Menu + "/" + option.Name;

            AddCommand({ .MenuPath = path, .Priority = i == 0 ? kCreateEmptyPriority : kCreatePresetPriority + static_cast<int>(i) - 1,
                         .Run = [this, option] { Create(option, {}); } });
        }
    }

    bool HierarchyPanel::IsSelectionEditable() const
    {
        const Entity selected = m_context.Session.GetSelected();
        return selected && !selected.HasComponent<InstancedComponent>();
    }

    void HierarchyPanel::Draw()
    {
        Scene& scene = m_context.Session.GetScene();

        if (const AssetInfo* info = m_context.Session.GetAssets().GetInfo(m_context.Session.GetSceneAsset()))
        {
            const std::string name = std::filesystem::path(info->Path).stem().string();
            ui::Text((m_context.Session.IsSceneChanged() ? "* " : "") + name, ui::TextStyle::Muted);
            ui::Tooltip(info->Path);
        }
        else
        {
            ui::Text("No scene is open, this won't be saved", ui::TextStyle::Muted);
        }

        DrawSearch();
        ui::Separator();

        const bool searching = m_filter.IsActive();
        m_shown.clear();

        if (searching)
        {
            for (const entt::entity root : scene.GetRoots())
                CollectMatches(root);
        }

        for (const entt::entity root : scene.GetRoots())
            DrawEntity(root);

        if (scene.GetRoots().empty())
            ui::EmptyHint("Right-click to create an entity, or drop a mesh into the Scene view");
        else if (searching && m_shown.empty())
            ui::EmptyHint("No entity has that in its name");

        // The rest of the panel: clicking clears the selection, dropping makes a root
        const glm::vec2 available = ui::Available();
        ui::HitArea("##HierarchyBackground", glm::vec2(std::max(available.x, 1.0f), std::max(available.y, 1.0f)));

        if (ui::item::Clicked())
            m_context.Session.SelectEntity({});

        if (const auto dropped = ui::drag_drop::Accept<entt::entity>(kEntityPayload))
        {
            m_deferred.emplace_back([this, dropped = *dropped]
            {
                const Entity moved = m_context.Session.GetScene().GetEntity(dropped);
                m_context.Session.GetSceneEdits().Record(moved);
                m_context.Session.GetScene().SetParent(moved, {});
            });
        }

        if (const ui::ContextMenu menu("##HierarchyContext"); menu)
            DrawCreateMenu({});

        // The entity may have gone while its name was being edited
        if (m_renaming != entt::null && !scene.GetRegistry().valid(m_renaming))
            m_renaming = entt::null;

        // They are edits made at once, so they are steps of their own right away
        if (!m_deferred.empty())
            m_context.Session.MarkEdited();

        for (const auto& action : m_deferred)
            action();

        m_deferred.clear();
    }

    void HierarchyPanel::DeleteEntity(Entity entity)
    {
        m_deferred.emplace_back([this, entity]
        {
            m_context.Session.GetSceneEdits().RecordDestroy(entity);
            m_context.Session.GetScene().DestroyEntity(entity);
        });
    }

    void HierarchyPanel::DrawSearch()
    {
        ui::NextItemFills();
        ui::SearchBox("##Search", m_filter, std::string(ui::icons::kSearch) + "  Search");
    }

    // Marks an entity to be shown when its name matches or something under it does, and says whether it is
    bool HierarchyPanel::CollectMatches(entt::entity handle)
    {
        const Entity entity = m_context.Session.GetScene().GetEntity(handle);

        bool shown = m_filter.Matches(entity.GetComponent<NameComponent>().Name);

        // Every child, not only up to the first match, as each one can be shown on its own
        for (const entt::entity child : entity.GetComponent<RelationshipComponent>().Children)
            shown = CollectMatches(child) || shown;

        if (shown)
            m_shown.insert(handle);

        return shown;
    }

    void HierarchyPanel::DrawEntity(entt::entity handle)
    {
        // A search shows only what matches and what leads to it, opened up to it
        const bool searching = m_filter.IsActive();
        if (searching && !m_shown.contains(handle))
            return;

        Scene& scene = m_context.Session.GetScene();
        const Entity entity = scene.GetEntity(handle);

        const auto& relationship = entity.GetComponent<RelationshipComponent>();
        const auto& name = entity.GetComponent<NameComponent>();

        // What comes from an instance's scene can only be looked at
        const bool readOnly = entity.HasComponent<InstancedComponent>();
        const bool isInstance = entity.HasComponent<SceneInstanceComponent>();

        const ui::IdScope scope(static_cast<int>(entt::to_integral(handle)));
        const bool renaming = handle == m_renaming;

        // Double-clicking renames, so only the arrow opens and closes
        const ui::TreeRow row("##Entity", ui::TreeRowOptions{
            .Icon = EntityIcon(entity, m_context.Session.GetAssets().Components()),
            .IconColor = ui::theme::TextMuted,
            .Label = renaming ? std::string() : isInstance ? name.Name + " [scene]" : name.Name,
            .Selected = m_context.Session.GetSelected() == entity,
            .Leaf = relationship.Children.empty(),
            .DefaultOpen = true,
            .ForceOpen = searching,
            .Muted = readOnly || !entity.GetComponent<TransformComponent>().ActiveInHierarchy
        });

        if (row.Clicked())
            m_context.Session.SelectEntity(entity);

        if (!readOnly && !renaming && ui::item::DoubleClicked() && !ui::item::ToggledOpen())
            BeginRename(entity);

        if (!readOnly)
            ui::drag_drop::Source(kEntityPayload, handle, name.Name);

        if (!readOnly && !isInstance)
        {
            if (const auto dropped = ui::drag_drop::Accept<entt::entity>(kEntityPayload))
            {
                m_deferred.emplace_back([this, dropped = *dropped, entity]
                {
                    const Entity moved = m_context.Session.GetScene().GetEntity(dropped);
                    m_context.Session.GetSceneEdits().Record(moved);
                    m_context.Session.GetScene().SetParent(moved, entity);
                });
            }
        }

        if (!readOnly)
        {
            if (const ui::ContextMenu menu("##EntityContext"); menu)
            {
                if (!isInstance)
                {
                    DrawCreateMenu(entity);
                    ui::Separator();
                }

                if (ui::MenuItem("Rename", ToText(kRename)))
                    BeginRename(entity);

                if (ui::MenuItem("Delete", ToText(kDelete)))
                    DeleteEntity(entity);
            }
        }

        if (renaming)
            DrawRenameField(entity);

        if (row.IsOpen())
        {
            for (const entt::entity child : relationship.Children)
                DrawEntity(child);
        }
    }

    void HierarchyPanel::BeginRename(Entity entity)
    {
        m_renameName = entity.GetComponent<NameComponent>().Name;
        m_renaming = entity.GetHandle();
        m_focusRename = true;
    }

    // Enter or clicking away keeps the new name, Escape keeps the old one
    void HierarchyPanel::DrawRenameField(Entity entity)
    {
        // The icon is drawn over the node and takes no room, so the field starts after it
        const char* icon = EntityIcon(entity, m_context.Session.GetAssets().Components());

        ui::Row row(true);
        row.Gap(ui::TextSize(icon).x + ui::InnerSpacing().x * 2.0f);
        ui::NextItemFills();

        const bool entered = ui::InputText("##Rename", m_renameName, ui::InputOptions{ .AutoSelectAll = true, .EnterConfirms = true, .Focus = m_focusRename });
        m_focusRename = false;

        if (!entered && !ui::item::Deactivated())
            return;

        m_renaming = entt::null;

        if (!entered && ui::input::KeyPressed(Key::Escape))
            return;

        // A name of only spaces would leave nothing to click in the tree
        std::string name = m_renameName;
        name.erase(0, name.find_first_not_of(" \t"));
        name.erase(name.find_last_not_of(" \t") + 1);

        if (!name.empty())
            m_deferred.emplace_back([this, entity, name = std::move(name)]
            {
                m_context.Session.GetSceneEdits().Record(entity);
                entity.GetComponent<NameComponent>().Name = name;
            });
    }

    std::vector<HierarchyPanel::CreateOption> HierarchyPanel::ListCreateOptions() const
    {
        std::vector<CreateOption> options{ CreateOption{ .Name = "Create Empty" } };

        // A submenu for each menu the components' presets name, in the order the components were registered
        const ComponentRegistry& components = m_context.Session.GetAssets().Components();

        std::vector<std::string_view> menus;
        for (const ComponentType& type : components.Types())
        {
            for (const ComponentPreset& preset : type.Presets)
            {
                if (std::ranges::find(menus, preset.Menu) == menus.end())
                    menus.push_back(preset.Menu);
            }
        }

        for (const std::string_view menu : menus)
        {
            for (const ComponentType& type : components.Types())
            {
                for (const ComponentPreset& preset : type.Presets)
                {
                    if (preset.Menu == menu)
                        options.push_back(CreateOption{ .Menu = std::string(menu), .Name = preset.Name, .Type = &type, .Preset = &preset });
                }
            }
        }

        return options;
    }

    void HierarchyPanel::Create(const CreateOption& option, Entity parent)
    {
        m_deferred.emplace_back([this, option, parent]
        {
            const Entity created = m_context.Session.GetScene().CreateEntity(option.Preset ? option.Preset->Name : "Entity", parent);
            if (option.Type)
                ApplyPreset(*option.Type, *option.Preset, created, m_context.Session.GetAssets());

            m_context.Session.GetSceneEdits().RecordCreated(created);
            m_context.Session.SelectEntity(created);
        });
    }

    void HierarchyPanel::DrawCreateMenu(Entity parent)
    {
        const std::vector<CreateOption> options = ListCreateOptions();

        for (size_t i = 0; i < options.size();)
        {
            if (options[i].Menu.empty())
            {
                if (ui::MenuItem(options[i].Name))
                    Create(options[i], parent);

                ++i;
                continue;
            }

            // The options of a menu are next to each other
            size_t end = i;
            while (end < options.size() && options[end].Menu == options[i].Menu)
                ++end;

            if (const ui::Menu menu("Create " + options[i].Menu); menu)
            {
                for (size_t j = i; j < end; ++j)
                {
                    if (ui::MenuItem(options[j].Name))
                        Create(options[j], parent);
                }
            }

            i = end;
        }
    }
}
