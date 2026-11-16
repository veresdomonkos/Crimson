#include <catch2/catch_test_macros.hpp>

#include "crimson_editor/ui/component_fields.hpp"
#include "crimson_editor/ui/icons.hpp"

using namespace crimson;
using namespace crimson::editor::ui;

TEST_CASE("Uniform scaling keeps the ratio of the axis that changed", "[component fields]")
{
    const glm::vec3 previous(1.0f, 2.0f, 4.0f);
    const glm::vec3 shape(1.0f);

    SECTION("Each axis sets the ratio for the others")
    {
        CHECK(ScaleUniformly(previous, glm::vec3(2.0f, 2.0f, 4.0f), shape) == glm::vec3(2.0f, 4.0f, 8.0f));
        CHECK(ScaleUniformly(previous, glm::vec3(1.0f, 1.0f, 4.0f), shape) == glm::vec3(0.5f, 1.0f, 2.0f));
        CHECK(ScaleUniformly(previous, glm::vec3(1.0f, 2.0f, 8.0f), shape) == glm::vec3(2.0f, 4.0f, 8.0f));
    }

    SECTION("An axis that was zero has no ratio, so the next one that changed is used")
    {
        const glm::vec3 flat(0.0f, 1.0f, 2.0f);
        CHECK(ScaleUniformly(flat, glm::vec3(5.0f, 3.0f, 2.0f), shape) == glm::vec3(0.0f, 3.0f, 6.0f));
        CHECK(ScaleUniformly(flat, glm::vec3(5.0f, 1.0f, 2.0f), shape) == glm::vec3(5.0f, 1.0f, 2.0f));
    }

    SECTION("Scaled to zero, it grows back with the proportions it had")
    {
        const glm::vec3 zero(0.0f);
        CHECK(ScaleUniformly(previous, glm::vec3(0.0f, 2.0f, 4.0f), shape) == zero);
        CHECK(ScaleUniformly(zero, glm::vec3(2.0f, 0.0f, 0.0f), previous) == glm::vec3(2.0f, 4.0f, 8.0f));
        CHECK(ScaleUniformly(zero, glm::vec3(0.0f, 0.0f, -2.0f), previous) == glm::vec3(-0.5f, -1.0f, -2.0f));
    }

    SECTION("Nothing changed")
    {
        CHECK(ScaleUniformly(previous, previous, shape) == previous);
    }
}

TEST_CASE("Icons are found by their Lucide name", "[component fields]")
{
    CHECK(icons::Named("sun") == icons::kDirectionalLight);
    CHECK(icons::Named("move-3d") == icons::kTransform);
    CHECK(icons::Named("box") == icons::kMesh);
    CHECK_FALSE(icons::Named("no-such-icon"));
    CHECK_FALSE(icons::Named(""));
}
