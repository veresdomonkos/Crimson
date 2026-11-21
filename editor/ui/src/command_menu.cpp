#include "command_menu.hpp"

#include <algorithm>
#include <string_view>

namespace crimson::editor::ui
{
    static MenuNode& FindOrAdd(MenuNode& parent, std::string_view name, int priority)
    {
        const auto found = std::ranges::find(parent.Children, name, &MenuNode::Name);
        if (found != parent.Children.end())
        {
            found->Lowest = std::min(found->Lowest, priority);
            found->Highest = std::max(found->Highest, priority);
            return *found;
        }

        return parent.Children.emplace_back(MenuNode{ .Name = std::string(name), .Lowest = priority, .Highest = priority });
    }

    // Orders the items of every menu, and puts the separators where the priorities are far apart
    static void Arrange(MenuNode& menu)
    {
        std::ranges::stable_sort(menu.Children, {}, &MenuNode::Lowest);

        for (size_t i = 0; i < menu.Children.size(); ++i)
        {
            Arrange(menu.Children[i]);

            if (i > 0 && menu.Children[i].Lowest - menu.Children[i - 1].Highest > kSeparatorGap)
                menu.Children[i].SeparatorBefore = true;
        }
    }

    MenuNode BuildMenu(std::span<const Command* const> commands)
    {
        MenuNode root;

        for (const Command* command : commands)
        {
            if (command->MenuPath.empty())
                continue;

            MenuNode* node = &root;
            std::string_view rest = command->MenuPath;

            while (true)
            {
                const size_t slash = rest.find('/');
                node = &FindOrAdd(*node, rest.substr(0, slash), command->Priority);

                if (slash == std::string_view::npos)
                    break;

                rest.remove_prefix(slash + 1);
            }

            node->Item = command;
        }

        Arrange(root);
        return root;
    }

    void SortForDrawing(std::vector<Panel*>& panels)
    {
        std::ranges::stable_sort(panels, [](const Panel* a, const Panel* b)
        {
            if (a->Info().Slot != b->Info().Slot)
                return a->Info().Slot < b->Info().Slot;

            return a->Info().TabOrder < b->Info().TabOrder;
        });
    }
}
