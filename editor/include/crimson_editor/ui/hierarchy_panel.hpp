#pragma once
#include <array>
#include <functional>
#include <vector>

#include "crimson_editor/editor_context.hpp"
#include "panel.hpp"

namespace crimson::editor::ui
{
    class HierarchyPanel : public Panel
    {
    public:
        explicit HierarchyPanel(EditorContext& context) : m_context(context) {}
        void OnImGui() override;
    private:
        void DrawEntity(entt::entity handle);
        void DrawCreateMenu(Entity parent);
        void BeginRename(Entity entity);
        void DrawRenameField(Entity entity);
    private:
        EditorContext& m_context;

        // Changes to the tree wait until the tree isn't being walked anymore
        std::vector<std::function<void()>> m_deferred;

        // The entity whose name is being edited in place, if any
        entt::entity m_renaming = entt::null;
        std::array<char, 256> m_renameBuffer{};
        bool m_focusRename = false;
    };
}
