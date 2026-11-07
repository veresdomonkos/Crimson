#include <catch2/catch_test_macros.hpp>

#include <crimson_editor/ui/asset_browser_panel.hpp>

using namespace crimson;
using crimson::editor::ui::IsEngineFolder;
using crimson::editor::ui::ListsAsset;

TEST_CASE("The Assets panel lists the engine's assets only when asked to", "[asset browser]")
{
    const AssetInfo project{ .Type = AssetType::Material, .Path = "materials/wood.mat" };
    const AssetInfo engine{ .Type = AssetType::Shader, .Path = "engine/shaders/S_Lit.hlsl", .ReadOnly = true };
    const AssetInfo internal{ .Type = AssetType::Shader, .Path = "engine/shaders/internal/S_ToneMap.hlsl", .ReadOnly = true };

    CHECK(ListsAsset(project, false));
    CHECK(ListsAsset(project, true));

    CHECK_FALSE(ListsAsset(engine, false));
    CHECK(ListsAsset(engine, true));

    // Only the renderer can use those
    CHECK_FALSE(ListsAsset(internal, false));
    CHECK_FALSE(ListsAsset(internal, true));
}

TEST_CASE("The engine's folder is told apart by its whole name", "[asset browser]")
{
    CHECK(IsEngineFolder("engine"));
    CHECK(IsEngineFolder("engine/meshes"));

    CHECK_FALSE(IsEngineFolder(""));
    CHECK_FALSE(IsEngineFolder("engines"));
    CHECK_FALSE(IsEngineFolder("engine_parts/meshes"));
    CHECK_FALSE(IsEngineFolder("my/engine"));
}
