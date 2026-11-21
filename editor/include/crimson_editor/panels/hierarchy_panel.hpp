#pragma once
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include "crimson_editor/editor_context.hpp"
#include "crimson_editor/ui/icons.hpp"
#include "crimson_editor/ui/panel.hpp"
#include "crimson_editor/ui/widgets.hpp"

namespace crimson::editor
{
    class HierarchyPanel : public ui::Panel
    {
    public:
        explicit HierarchyPanel(EditorContext& context);
    protected:
        void Draw() override;
    private:
        // What the Create menus offer: Create Empty, and a preset of a component for each of the components'
        struct CreateOption
        {
            std::string Menu; // empty for Create Empty
            std::string Name;
            const ComponentType* Type = nullptr;
            const ComponentPreset* Preset = nullptr;
        };

        // Grouped by menu, in the order the menus and the components were registered
        [[nodiscard]] std::vector<CreateOption> ListCreateOptions() const;
        void Create(const CreateOption& option, Entity parent);

        // What comes from an instance's scene can only be looked at
        [[nodiscard]] bool IsSelectionEditable() const;

        void DrawSearch();
        bool CollectMatches(entt::entity handle);
        void DrawEntity(entt::entity handle);
        void DrawCreateMenu(Entity parent);
        void BeginRename(Entity entity);
        void DrawRenameField(Entity entity);
        void DeleteEntity(Entity entity);
    private:
        EditorContext& m_context;

        // Changes to the tree wait until the tree isn't being walked anymore
        std::vector<std::function<void()>> m_deferred;

        // The entity whose name is being edited in place, if any
        entt::entity m_renaming = entt::null;
        std::string m_renameName;
        bool m_focusRename = false;

        // While searching, what matches and what has a match under it, as only those are shown
        ui::SearchFilter m_filter;
        std::unordered_set<entt::entity> m_shown;
    };
}
