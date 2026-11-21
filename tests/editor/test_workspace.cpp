#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <imgui.h>

#include "crimson_editor/ui/imgui_platform.hpp"
#include "crimson_editor/ui/workspace.hpp"
#include "support/fake_window.hpp"
#include "support/imgui_context.hpp"

using namespace crimson;
using namespace crimson::editor::ui;
using namespace crimson::tests;

namespace
{
    // A panel that counts what the workspace does with it
    class CountingPanel : public Panel
    {
    public:
        explicit CountingPanel(PanelInfo info) : Panel(std::move(info)) {}

        void Add(Command command) { AddCommand(std::move(command)); }

        int Drawn = 0;
        int Opened = 0;
    protected:
        void Draw() override { ++Drawn; }
        void OnOpened() override { ++Opened; }
    };

    // A panel with a text field that has the keyboard from the start
    class TypingPanel : public CountingPanel
    {
    public:
        TypingPanel() : CountingPanel({ .Title = "Typing" }) {}
    protected:
        void Draw() override
        {
            CountingPanel::Draw();

            if (!m_focused)
            {
                ImGui::SetKeyboardFocusHere();
                m_focused = true;
            }

            ImGui::InputText("##Text", m_text, sizeof(m_text));
        }
    private:
        char m_text[32] = {};
        bool m_focused = false;
    };

    // A workspace in a context of its own, drawn without a GPU, and a keyboard
    struct Frames
    {
        void Run(int count = 1)
        {
            for (int i = 0; i < count; ++i)
            {
                Platform.NewFrame(1.0f / 60.0f);
                ImGui::NewFrame();
                Workspace.Draw();
                ImGui::EndFrame();
            }
        }

        // Each is in a frame of its own, as a route given to a shortcut starts the frame after
        void Press(Key key, KeyModifiers modifiers = KeyModifiers::None)
        {
            KeyPressEvent press(key, modifiers, false);
            Platform.OnEvent(press);
            Run();

            KeyReleaseEvent release(key, KeyModifiers::None);
            Platform.OnEvent(release);
            Run();
        }

        ImGuiContextGuard Context;
        FakeWindow Screen;
        ImGuiPlatform Platform{ Screen };
        crimson::editor::ui::Workspace Workspace;
    };
}

TEST_CASE("A panel's shortcut fires only while it is focused", "[editor][workspace]")
{
    Frames frames;

    int ranInA = 0;
    int ranInB = 0;

    auto& a = frames.Workspace.AddPanel<CountingPanel>(PanelInfo{ .Title = "A", .Slot = DockSlot::Left });
    auto& b = frames.Workspace.AddPanel<CountingPanel>(PanelInfo{ .Title = "B", .Slot = DockSlot::Right });
    a.Add({ .Shortcuts = { { Key::F2 } }, .Run = [&] { ++ranInA; } });
    b.Add({ .Shortcuts = { { Key::F2 } }, .Run = [&] { ++ranInB; } });

    frames.Run(3);

    ImGui::SetWindowFocus("B");
    frames.Run(2);
    frames.Press(Key::F2);

    CHECK(ranInA == 0);
    CHECK(ranInB == 1);

    ImGui::SetWindowFocus("A");
    frames.Run(2);
    frames.Press(Key::F2);

    CHECK(ranInA == 1);
    CHECK(ranInB == 1);
}

TEST_CASE("A plain-key shortcut waits while text is typed, a Ctrl one doesn't unless the field uses it", "[editor][workspace]")
{
    Frames frames;

    int frame = 0;
    int save = 0;
    int undo = 0;

    frames.Workspace.AddPanel<TypingPanel>();
    frames.Workspace.AddCommand({ .Shortcuts = { { Key::F } }, .Run = [&] { ++frame; } });
    frames.Workspace.AddCommand({ .Shortcuts = { { Key::S, KeyModifiers::Control } }, .Run = [&] { ++save; } });
    frames.Workspace.AddCommand({ .Shortcuts = { { Key::Z, KeyModifiers::Control } }, .Repeats = true, .Run = [&] { ++undo; } });

    frames.Run(4);
    REQUIRE(ImGui::GetIO().WantTextInput);

    frames.Press(Key::F);
    CHECK(frame == 0);

    // Saving has nothing to do with text
    frames.Press(Key::S, KeyModifiers::Control);
    CHECK(save == 1);

    // The field undoes its own typing
    frames.Press(Key::Z, KeyModifiers::Control);
    CHECK(undo == 0);
}

TEST_CASE("A closed panel isn't drawn, its commands neither show nor fire, and Show opens it again", "[editor][workspace]")
{
    Frames frames;

    int ran = 0;

    auto& panel = frames.Workspace.AddPanel<CountingPanel>(PanelInfo{ .Title = "Closed", .OpenAtStart = false });
    panel.Add({ .MenuPath = "Edit/Thing", .Shortcuts = { { Key::F2 } }, .Run = [&] { ++ran; } });

    frames.Run(3);
    frames.Press(Key::F2);

    CHECK_FALSE(panel.IsOpen());
    CHECK(panel.Drawn == 0);
    CHECK(ran == 0);

    panel.Show();
    frames.Run(3);

    CHECK(panel.IsOpen());
    CHECK(panel.Drawn > 0);
    CHECK(panel.Opened == 1);

    // Show focused it, so its shortcut works
    frames.Press(Key::F2);
    CHECK(ran == 1);

    // Showing what is open doesn't open it again
    panel.Show();
    CHECK(panel.Opened == 1);

    panel.Close();
    frames.Run(2);
    const int drawn = panel.Drawn;
    frames.Run(2);

    CHECK(panel.Drawn == drawn);
}
