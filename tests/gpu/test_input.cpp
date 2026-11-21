#include <catch2/catch_test_macros.hpp>

#include <GLFW/glfw3.h>

#include <crimson/core/input.hpp>

#include "gpu/gpu_test.hpp"

using namespace crimson;
using namespace crimson::tests;

static glm::dvec2 CursorPosition(GLFWwindow* window)
{
    glm::dvec2 position;
    glfwGetCursorPos(window, &position.x, &position.y);
    return position;
}

// A hidden cursor that wandered off would hover and click the panels it ended up over
TEST_CASE("A locked cursor stays where it was locked, and its movement still counts", "[input]")
{
    auto* window = static_cast<GLFWwindow*>(GpuWindow().GetNativeHandle());
    Input input(GpuWindow());

    glfwSetCursorPos(window, 100.0, 80.0);
    input.Update();

    input.SetCursorLocked(true);
    REQUIRE(input.IsCursorLocked());
    input.Update();
    const glm::dvec2 lockedAt = CursorPosition(window);

    glfwSetCursorPos(window, lockedAt.x + 40.0, lockedAt.y - 25.0);
    input.Update();

    CHECK(input.GetMouseDelta() == glm::vec2(40.0f, -25.0f));
    CHECK(CursorPosition(window) == lockedAt);

    // Moving again counts from where it was put back
    glfwSetCursorPos(window, lockedAt.x + 5.0, lockedAt.y);
    input.Update();
    CHECK(input.GetMouseDelta() == glm::vec2(5.0f, 0.0f));

    input.SetCursorLocked(false);
    CHECK_FALSE(input.IsCursorLocked());
    CHECK(glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_NORMAL);
}

TEST_CASE("The window gives its OS handle", "[window]")
{
    const OSWindowHandle handle = GpuWindow().GetOSHandle();

#ifdef _WIN32
    CHECK(handle.Type == OSWindowHandle::Kind::Win32);
#elif defined(__APPLE__)
    CHECK(handle.Type == OSWindowHandle::Kind::Cocoa);
#else
    // CI builds GLFW without Wayland
    CHECK(handle.Type == OSWindowHandle::Kind::X11);
#endif
    CHECK(handle.Value != 0);
}
