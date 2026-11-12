#include <catch2/catch_test_macros.hpp>

#include <set>
#include <utility>
#include <vector>

#include <crimson/renderer/shadow_atlas.hpp>

using namespace crimson;

static Light MakeLight(LightType type, bool castsShadow = true)
{
    Light light;
    light.Type = type;
    light.CastsShadow = castsShadow;
    return light;
}

TEST_CASE("Each kind of light takes its number of tiles", "[shadows]")
{
    std::vector<Light> lights = {
        MakeLight(LightType::Directional),
        MakeLight(LightType::Point),
        MakeLight(LightType::Spot),
        MakeLight(LightType::Point, false)
    };
    std::vector<ShadowView> views;

    const uint32_t skipped = BuildShadowViews(lights, ShadowAtlasSettings{}, views);

    CHECK(skipped == 0);
    CHECK(views.size() == 1 + 6 + 1);

    // A light's index is where its views start
    CHECK(lights[0].ShadowIndex == 0);
    CHECK(lights[1].ShadowIndex == 1);
    CHECK(lights[2].ShadowIndex == 7);
    CHECK(lights[3].ShadowIndex == -1);
}

TEST_CASE("Lights that don't fit in the atlas don't cast shadows", "[shadows]")
{
    // Room for 4 tiles, so a point light can never fit, but a later spot light still can
    const ShadowAtlasSettings settings{ .AtlasSize = 2048, .TileSize = 1024 };

    std::vector<Light> lights = {
        MakeLight(LightType::Spot),
        MakeLight(LightType::Point),
        MakeLight(LightType::Spot)
    };
    std::vector<ShadowView> views;

    CHECK(BuildShadowViews(lights, settings, views) == 1);
    CHECK(views.size() == 2);
    CHECK(lights[0].ShadowIndex == 0);
    CHECK(lights[1].ShadowIndex == -1);
    CHECK(lights[2].ShadowIndex == 1);
}

TEST_CASE("Tiles don't overlap and stay inside the atlas", "[shadows]")
{
    const ShadowAtlasSettings settings;

    std::vector<Light> lights(2, MakeLight(LightType::Point));
    std::vector<ShadowView> views;
    BuildShadowViews(lights, settings, views);

    std::set<std::pair<int32_t, int32_t>> corners;

    for (const ShadowView& view : views)
    {
        corners.insert({ view.Viewport.X, view.Viewport.Y });

        CHECK(view.Viewport.X + static_cast<int32_t>(view.Viewport.Width) <= static_cast<int32_t>(settings.AtlasSize));
        CHECK(view.Viewport.Y + static_cast<int32_t>(view.Viewport.Height) <= static_cast<int32_t>(settings.AtlasSize));

        // What the shader samples has to be the same area the pass renders to
        CHECK(view.Shadow.AtlasRect.x * settings.AtlasSize == static_cast<float>(view.Viewport.X));
        CHECK(view.Shadow.AtlasRect.y * settings.AtlasSize == static_cast<float>(view.Viewport.Y));
    }

    CHECK(corners.size() == views.size());
}

TEST_CASE("Building again starts over", "[shadows]")
{
    std::vector<Light> lights = { MakeLight(LightType::Spot) };
    std::vector<ShadowView> views;

    BuildShadowViews(lights, ShadowAtlasSettings{}, views);
    lights[0].CastsShadow = false;
    BuildShadowViews(lights, ShadowAtlasSettings{}, views);

    CHECK(views.empty());
    CHECK(lights[0].ShadowIndex == -1);
}

TEST_CASE("A shadow view looks from its light", "[shadows]")
{
    const ShadowAtlasSettings settings;

    Light point = MakeLight(LightType::Point);
    point.Position = glm::vec3(1.0f, 2.0f, 3.0f);

    Light spot = MakeLight(LightType::Spot);
    spot.Position = glm::vec3(-4.0f, 5.0f, 6.0f);

    Light directional = MakeLight(LightType::Directional);
    directional.Direction = glm::vec3(0.0f, 0.0f, -1.0f);

    std::vector<Light> lights = { point, spot, directional };
    std::vector<ShadowView> views;
    BuildShadowViews(lights, settings, views);

    REQUIRE(views.size() == 6 + 1 + 1);

    for (uint32_t i = 0; i < 6; ++i)
        CHECK(views[i].Eye == point.Position);

    CHECK(views[6].Eye == spot.Position);

    // A directional light has no position, it looks at the shadowed area from far back along its direction
    CHECK(views[7].Eye == settings.DirectionalCenter + glm::vec3(0.0f, 0.0f, 1.0f) * settings.DirectionalDistance);
}

TEST_CASE("A light looking straight down still gets a usable view", "[shadows]")
{
    Light light = MakeLight(LightType::Directional);
    light.Direction = glm::vec3(0.0f, -1.0f, 0.0f);

    std::vector<Light> lights = { light };
    std::vector<ShadowView> views;
    BuildShadowViews(lights, ShadowAtlasSettings{}, views);

    REQUIRE(views.size() == 1);

    // An up vector parallel to the direction would have filled the matrix with NaNs
    const glm::mat4& matrix = views[0].Shadow.ViewProj;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            CHECK(matrix[column][row] == matrix[column][row]);
}
