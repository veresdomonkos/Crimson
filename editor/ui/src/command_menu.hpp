#pragma once
#include <span>
#include <string>
#include <vector>

#include "crimson_editor/ui/commands.hpp"
#include "crimson_editor/ui/panel.hpp"

namespace crimson::editor::ui
{
    // A gap in priorities between two items of a menu bigger than this puts a separator between them
    constexpr int kSeparatorGap = 10;

    // A menu, an item of one, or the root of a menu bar
    struct MenuNode
    {
        std::string Name;

        // The lowest and the highest priority among the commands in it, which is where it sits and where it ends
        int Lowest = 0;
        int Highest = 0;

        bool SeparatorBefore = false;

        // The command of an item. Null for a menu, which has its Children.
        const Command* Item = nullptr;
        std::vector<MenuNode> Children;
    };

    // The menu bar that the paths of the commands make: menus ordered by their lowest priority, with their items in
    // order too. A command without a path isn't in it. The commands have to outlive it.
    [[nodiscard]] MenuNode BuildMenu(std::span<const Command* const> commands);

    // Orders panels by their slot and then their tab order, whatever order they were added in
    void SortForDrawing(std::vector<Panel*>& panels);
}
