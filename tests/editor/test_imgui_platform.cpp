#include <string>

#include <catch2/catch_test_macros.hpp>
#include <imgui.h>

#include "crimson_editor/ui/imgui_platform.hpp"
#include "support/fake_window.hpp"
#include "support/imgui_context.hpp"

using namespace crimson;
using namespace crimson::editor::ui;
using namespace crimson::tests;

namespace
{
    void Send(ImGuiPlatform& platform, Event&& event)
    {
        platform.OnEvent(event);
    }
}

TEST_CASE("ImGui gets its input from the window's events", "[editor][imgui]")
{
    const ImGuiContextGuard context;

    FakeWindow window;
    window.SetSize(640, 480);
    ImGuiPlatform platform(window);

    Send(platform, KeyPressEvent(Key::A, KeyModifiers::Control, false));
    Send(platform, TextInputEvent(U'x'));
    Send(platform, MouseButtonPressEvent(MouseButton::Left, KeyModifiers::Control));
    Send(platform, MouseMoveEvent({ 10.0f, 20.0f }, { 10.0f, 20.0f }));
    Send(platform, MouseScrollEvent({ 0.0f, 2.0f }));
    Send(platform, WindowFocusEvent(true));

    platform.NewFrame(1.0f / 60.0f);
    ImGui::NewFrame();

    const ImGuiIO& io = ImGui::GetIO();

    CHECK(ImGui::IsKeyDown(ImGuiKey_A));
    CHECK(io.KeyCtrl);
    CHECK_FALSE(io.KeyShift);
    REQUIRE(io.InputQueueCharacters.Size == 1);
    CHECK(io.InputQueueCharacters[0] == U'x');
    CHECK(ImGui::IsMouseDown(0));
    CHECK_FALSE(ImGui::IsMouseDown(1));
    CHECK(io.MousePos.x == 10.0f);
    CHECK(io.MousePos.y == 20.0f);
    CHECK(io.MouseWheel == 2.0f);
    CHECK(io.DisplaySize.x == 640.0f);
    CHECK(io.DisplaySize.y == 480.0f);

    ImGui::EndFrame();

    // The release and the mouse leaving
    Send(platform, KeyReleaseEvent(Key::A, KeyModifiers::None));
    Send(platform, MouseButtonReleaseEvent(MouseButton::Left, KeyModifiers::None));
    Send(platform, MouseLeaveEvent());

    platform.NewFrame(1.0f / 60.0f);
    ImGui::NewFrame();

    CHECK_FALSE(ImGui::IsKeyDown(ImGuiKey_A));
    CHECK_FALSE(io.KeyCtrl);
    CHECK_FALSE(ImGui::IsMouseDown(0));
    CHECK_FALSE(ImGui::IsMousePosValid());

    ImGui::EndFrame();
}

TEST_CASE("ImGui's clipboard and cursor go to the window", "[editor][imgui]")
{
    const ImGuiContextGuard context;

    FakeWindow window;
    ImGuiPlatform platform(window);

    ImGui::SetClipboardText("copied");
    CHECK(window.Clipboard == "copied");

    window.Clipboard = "pasted";
    CHECK(std::string(ImGui::GetClipboardText()) == "pasted");

    platform.NewFrame(1.0f / 60.0f);
    ImGui::NewFrame();
    ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    ImGui::EndFrame();

    CHECK(window.Shape == CursorShape::Arrow);

    platform.NewFrame(1.0f / 60.0f);
    CHECK(window.Shape == CursorShape::TextInput);
}
