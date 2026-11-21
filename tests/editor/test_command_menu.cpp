#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "command_menu.hpp"

using namespace crimson;
using namespace crimson::editor::ui;

namespace
{
    Command MakeCommand(std::string path, int priority)
    {
        return Command{ .MenuPath = std::move(path), .Priority = priority, .Run = [] {} };
    }

    MenuNode Build(const std::vector<Command>& commands)
    {
        std::vector<const Command*> pointers;
        for (const Command& command : commands)
            pointers.push_back(&command);

        return BuildMenu(pointers);
    }

    class TestPanel : public Panel
    {
    public:
        TestPanel(std::string title, DockSlot slot, int tabOrder)
            : Panel({ .Title = std::move(title), .Slot = slot, .TabOrder = tabOrder }) {}
    protected:
        void Draw() override {}
    };
}

TEST_CASE("Commands are ordered by priority in their menu, with a separator after a gap of more than 10", "[editor][commands]")
{
    // Added in no particular order
    const std::vector<Command> commands = {
        MakeCommand("Edit/Rename", 120), MakeCommand("Edit/Undo", 100),
        MakeCommand("Edit/Preferences...", 150), MakeCommand("Edit/Redo", 101)
    };

    const MenuNode root = Build(commands);
    REQUIRE(root.Children.size() == 1);

    const MenuNode& edit = root.Children[0];
    CHECK(edit.Name == "Edit");
    REQUIRE(edit.Children.size() == 4);

    CHECK(edit.Children[0].Name == "Undo");
    CHECK(edit.Children[1].Name == "Redo");
    CHECK(edit.Children[2].Name == "Rename");
    CHECK(edit.Children[3].Name == "Preferences...");

    CHECK_FALSE(edit.Children[0].SeparatorBefore);
    CHECK_FALSE(edit.Children[1].SeparatorBefore);
    CHECK(edit.Children[2].SeparatorBefore);
    CHECK(edit.Children[3].SeparatorBefore);

    CHECK(edit.Children[0].Item == &commands[1]);
}

TEST_CASE("Menu paths nest, and a submenu sits by its lowest priority", "[editor][commands]")
{
    const std::vector<Command> commands = {
        MakeCommand("Create/Create Light/Spot Light", 211), MakeCommand("Create/Create Light/Point Light", 210),
        MakeCommand("Create/Create Empty", 200), MakeCommand("Create/Create Mesh/Cube", 212)
    };

    const MenuNode root = Build(commands);
    REQUIRE(root.Children.size() == 1);

    const MenuNode& create = root.Children[0];
    REQUIRE(create.Children.size() == 3);

    CHECK(create.Children[0].Name == "Create Empty");
    CHECK(create.Children[0].Item != nullptr);

    // A menu has its items, and no command of its own
    const MenuNode& lights = create.Children[1];
    CHECK(lights.Name == "Create Light");
    CHECK(lights.Item == nullptr);
    CHECK(lights.Lowest == 210);
    REQUIRE(lights.Children.size() == 2);
    CHECK(lights.Children[0].Name == "Point Light");
    CHECK(lights.Children[1].Name == "Spot Light");

    // The gap is from where the item before ends, and there is none here
    CHECK_FALSE(lights.SeparatorBefore);
    CHECK(create.Children[2].Name == "Create Mesh");
}

TEST_CASE("Top-level menus are ordered by their lowest priority, not by when they were added", "[editor][commands]")
{
    const std::vector<Command> commands = {
        MakeCommand("Edit/Undo", 100), MakeCommand("Create/Create Empty", 200), MakeCommand("File/Save All", 0)
    };

    const MenuNode root = Build(commands);
    REQUIRE(root.Children.size() == 3);

    CHECK(root.Children[0].Name == "File");
    CHECK(root.Children[1].Name == "Edit");
    CHECK(root.Children[2].Name == "Create");
}

TEST_CASE("A command without a menu path isn't in the menu", "[editor][commands]")
{
    const std::vector<Command> commands = { MakeCommand("", 0), MakeCommand("File/Save All", 0) };

    const MenuNode root = Build(commands);
    REQUIRE(root.Children.size() == 1);
    CHECK(root.Children[0].Name == "File");
}

TEST_CASE("Shortcuts read as they are pressed", "[editor][commands]")
{
    CHECK(ToText({ Key::S, KeyModifiers::Control }) == "Ctrl+S");
    CHECK(ToText({ Key::Z, KeyModifiers::Control | KeyModifiers::Shift }) == "Ctrl+Shift+Z");
    CHECK(ToText({ Key::F2 }) == "F2");
    CHECK(ToText({ Key::Delete }) == "Delete");
}

TEST_CASE("Panels draw in tab order within each slot, whatever order they were added in", "[editor][commands]")
{
    std::vector<std::unique_ptr<Panel>> owned;
    owned.push_back(std::make_unique<TestPanel>("Inspector", DockSlot::Right, 0));
    owned.push_back(std::make_unique<TestPanel>("Console", DockSlot::Bottom, 1));
    owned.push_back(std::make_unique<TestPanel>("Stats", DockSlot::RightBottom, 1));
    owned.push_back(std::make_unique<TestPanel>("Hierarchy", DockSlot::Left, 0));
    owned.push_back(std::make_unique<TestPanel>("Assets", DockSlot::Bottom, 0));
    owned.push_back(std::make_unique<TestPanel>("Shadow Map", DockSlot::RightBottom, 0));

    std::vector<Panel*> panels;
    for (const auto& panel : owned)
        panels.push_back(panel.get());

    SortForDrawing(panels);

    const std::vector<std::string> expected = { "Hierarchy", "Assets", "Console", "Inspector", "Shadow Map", "Stats" };
    REQUIRE(panels.size() == expected.size());

    for (size_t i = 0; i < expected.size(); ++i)
        CHECK(panels[i]->Info().Title == expected[i]);
}
