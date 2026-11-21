#include <catch2/catch_test_macros.hpp>

#include <crimson/core/window.hpp>

#include "gpu/gpu_test.hpp"

using namespace crimson;
using namespace crimson::tests;

// A hidden cursor that wandered off would hover and click the panels it ended up over. Where it stays is checked by the
// CursorTracker unit tests, as a real cursor can't be moved from here.
TEST_CASE("Locking the cursor of a real window", "[input]")
{
    Window& window = GpuWindow();
    REQUIRE_FALSE(window.IsCursorLocked());

    window.SetCursorLocked(true);
    CHECK(window.IsCursorLocked());

    // The cursor is hidden then, so a shape is ignored
    window.SetCursorShape(CursorShape::Hand);

    // Locking again, or unlocking twice, changes nothing and logs no error
    window.SetCursorLocked(true);
    CHECK(window.IsCursorLocked());

    window.SetCursorLocked(false);
    CHECK_FALSE(window.IsCursorLocked());

    window.SetCursorLocked(false);
    CHECK_FALSE(window.IsCursorLocked());
}

TEST_CASE("The window gives its OS handle", "[window]")
{
    const OSWindowHandle handle = GpuWindow().GetOSHandle();

#ifdef _WIN32
    CHECK(handle.Type == OSWindowHandle::Kind::Win32);
#elif defined(__APPLE__)
    CHECK(handle.Type == OSWindowHandle::Kind::Cocoa);
#else
    CHECK(handle.Type == OSWindowHandle::Kind::X11);
#endif
    CHECK(handle.Value != 0);
}

TEST_CASE("The window's clipboard keeps text, and every cursor shape can be set", "[window]")
{
    Window& window = GpuWindow();

    // A check mark, in UTF-8
    const std::string text = "Crimson \xE2\x9C\x93";
    window.SetClipboardText(text);
    CHECK(window.GetClipboardText() == text);

    window.SetClipboardText("");
    CHECK(window.GetClipboardText().empty());

    for (const CursorShape shape : { CursorShape::Arrow, CursorShape::TextInput, CursorShape::ResizeAll,
                                     CursorShape::ResizeVertical, CursorShape::ResizeHorizontal,
                                     CursorShape::ResizeDiagonalUp, CursorShape::ResizeDiagonalDown,
                                     CursorShape::Hand, CursorShape::NotAllowed })
    {
        window.SetCursorShape(shape);
    }

    window.SetCursorShape(CursorShape::Arrow);
}
