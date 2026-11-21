#include <catch2/catch_test_macros.hpp>

#include <crimson/core/events.hpp>
#include <crimson/core/input.hpp>

#include "platform/cursor_tracker.hpp"

using namespace crimson;

static void Send(Input& input, Event&& event)
{
    input.OnEvent(event);
}

TEST_CASE("Input follows the window's events", "[input]")
{
    Input input;

    SECTION("A key is down from its press until its release, repeats included")
    {
        CHECK_FALSE(input.IsKeyDown(Key::W));

        Send(input, KeyPressEvent(Key::W, KeyModifiers::None, false));
        CHECK(input.IsKeyDown(Key::W));
        CHECK_FALSE(input.IsKeyDown(Key::S));

        Send(input, KeyPressEvent(Key::W, KeyModifiers::None, true));
        CHECK(input.IsKeyDown(Key::W));

        Send(input, KeyReleaseEvent(Key::W, KeyModifiers::None));
        CHECK_FALSE(input.IsKeyDown(Key::W));
    }

    SECTION("A mouse button is down from its press until its release")
    {
        Send(input, MouseButtonPressEvent(MouseButton::Right, KeyModifiers::None));
        CHECK(input.IsMouseButtonDown(MouseButton::Right));
        CHECK_FALSE(input.IsMouseButtonDown(MouseButton::Left));

        Send(input, MouseButtonReleaseEvent(MouseButton::Right, KeyModifiers::None));
        CHECK_FALSE(input.IsMouseButtonDown(MouseButton::Right));
    }

    SECTION("The moves of a frame add up at Update, and the next frame starts from nothing")
    {
        Send(input, MouseMoveEvent({ 10.0f, 20.0f }, { 10.0f, 20.0f }));
        Send(input, MouseMoveEvent({ 15.0f, 18.0f }, { 5.0f, -2.0f }));

        CHECK(input.GetMouseDelta() == glm::vec2(0.0f));
        CHECK(input.GetMousePosition() == glm::vec2(15.0f, 18.0f));

        input.Update();
        CHECK(input.GetMouseDelta() == glm::vec2(15.0f, 18.0f));

        input.Update();
        CHECK(input.GetMouseDelta() == glm::vec2(0.0f));
        CHECK(input.GetMousePosition() == glm::vec2(15.0f, 18.0f));
    }

    SECTION("Events of other kinds change nothing, and stay unhandled")
    {
        MouseScrollEvent scroll({ 0.0f, 1.0f });
        input.OnEvent(scroll);

        KeyPressEvent press(Key::A, KeyModifiers::Control, false);
        press.Handled = false;
        input.OnEvent(press);

        CHECK_FALSE(press.Handled);
        CHECK_FALSE(input.IsKeyDown(Key::B));
        CHECK(input.GetMouseDelta() == glm::vec2(0.0f));
    }
}

TEST_CASE("A locked cursor reports its locked position, and its movement", "[input]")
{
    CursorTracker cursor;

    // The first position only becomes the origin
    MouseMoveEvent first = cursor.Move({ 100.0f, 80.0f });
    CHECK(first.GetPosition() == glm::vec2(100.0f, 80.0f));
    CHECK(first.GetDelta() == glm::vec2(0.0f));

    MouseMoveEvent free = cursor.Move({ 103.0f, 85.0f });
    CHECK(free.GetPosition() == glm::vec2(103.0f, 85.0f));
    CHECK(free.GetDelta() == glm::vec2(3.0f, 5.0f));

    cursor.Lock({ 103.0f, 85.0f });
    REQUIRE(cursor.IsLocked());

    // The mode change makes the position jump, which isn't movement
    MouseMoveEvent jump = cursor.Move({ 500.0f, 500.0f });
    CHECK(jump.GetPosition() == glm::vec2(103.0f, 85.0f));
    CHECK(jump.GetDelta() == glm::vec2(0.0f));

    MouseMoveEvent locked = cursor.Move({ 540.0f, 475.0f });
    CHECK(locked.GetPosition() == glm::vec2(103.0f, 85.0f));
    CHECK(locked.GetDelta() == glm::vec2(40.0f, -25.0f));

    // Moving again counts from where it was
    MouseMoveEvent again = cursor.Move({ 545.0f, 475.0f });
    CHECK(again.GetPosition() == glm::vec2(103.0f, 85.0f));
    CHECK(again.GetDelta() == glm::vec2(5.0f, 0.0f));

    CHECK(cursor.Unlock() == glm::vec2(103.0f, 85.0f));
    CHECK_FALSE(cursor.IsLocked());

    // The window puts the cursor back, which isn't movement either
    MouseMoveEvent back = cursor.Move({ 103.0f, 85.0f });
    CHECK(back.GetPosition() == glm::vec2(103.0f, 85.0f));
    CHECK(back.GetDelta() == glm::vec2(0.0f));

    MouseMoveEvent after = cursor.Move({ 110.0f, 85.0f });
    CHECK(after.GetPosition() == glm::vec2(110.0f, 85.0f));
    CHECK(after.GetDelta() == glm::vec2(7.0f, 0.0f));
}
