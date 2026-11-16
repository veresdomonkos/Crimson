#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <crimson_editor/ui/linear_color.hpp>

using namespace crimson::editor::ui;
using Catch::Approx;

TEST_CASE("Colors convert between linear light and sRGB", "[editor]")
{
    CHECK(linear_color::ToSrgb(0.0f) == 0.0f);
    CHECK(linear_color::ToSrgb(1.0f) == Approx(1.0f));
    CHECK(linear_color::ToSrgb(0.5f) == Approx(188.0f / 255.0f).margin(0.002f));

    CHECK(linear_color::FromSrgb(1.0f) == Approx(1.0f));
    CHECK(linear_color::FromSrgb(188.0f / 255.0f) == Approx(0.5f).margin(0.004f));

    for (const float value : { 0.001f, 0.01f, 0.2f, 0.75f })
        CHECK(linear_color::FromSrgb(linear_color::ToSrgb(value)) == Approx(value));
}
