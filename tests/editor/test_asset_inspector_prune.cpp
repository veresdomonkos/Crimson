#include <catch2/catch_test_macros.hpp>

#include <crimson_editor/ui/asset_inspector.hpp>

using namespace crimson;
using crimson::editor::ui::PruneToShader;

TEST_CASE("A material keeps what its shader has and drops the rest", "[asset inspector]")
{
    ShaderInterface shader;
    shader.Properties["Color"] = {};
    shader.Textures["ColorMap"] = {};

    MaterialData data;
    data.Properties["Color"] = { 1.0f, 0.0f, 0.0f, 1.0f };
    data.Properties["Glow"] = { 1.0f };
    data.Textures["ColorMap"] = {};
    data.Textures["NormalMap"] = {};

    CHECK(PruneToShader(data, shader));
    CHECK(data.Properties.size() == 1);
    CHECK(data.Properties.contains("Color"));
    CHECK(data.Textures.size() == 1);
    CHECK(data.Textures.contains("ColorMap"));

    // Going back to a shader that has everything drops nothing
    CHECK_FALSE(PruneToShader(data, shader));
}
