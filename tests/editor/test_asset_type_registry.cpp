#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <crimson_editor/ui/asset_type_registry.hpp>
#include <crimson_editor/ui/icons.hpp>

using namespace crimson;
using namespace crimson::editor::ui;

TEST_CASE("Every asset type is registered once", "[asset types]")
{
    AssetTypeRegistry registry;
    RegisterAssetTypes(registry);

    CHECK(registry.Types().size() == kAssetTypeCount - 1);

    for (size_t index = 1; index < kAssetTypeCount; ++index)
    {
        const auto type = static_cast<AssetType>(index);
        const EditorAssetType* found = registry.Find(type);

        REQUIRE(found);
        CHECK(found->Type == type);
    }
}

TEST_CASE("An unregistered type looks like a plain file", "[asset types]")
{
    AssetTypeRegistry registry;
    RegisterAssetTypes(registry);

    CHECK(registry.Find(AssetType::None) == nullptr);
    CHECK(std::string_view(registry.Get(AssetType::None).Icon) == icons::kFile);
}

TEST_CASE("Only textures have thumbnails", "[asset types]")
{
    AssetTypeRegistry registry;
    RegisterAssetTypes(registry);

    for (const EditorAssetType& type : registry.Types())
        CHECK(static_cast<bool>(type.Thumbnail) == (type.Type == AssetType::Texture));
}

TEST_CASE("Every registered type has an Inspector view", "[asset types]")
{
    AssetTypeRegistry registry;
    RegisterAssetTypes(registry);

    for (const EditorAssetType& type : registry.Types())
        CHECK(static_cast<bool>(type.MakeView));
}

TEST_CASE("The create menu offers materials, scenes and shaders, in that order", "[asset types]")
{
    AssetTypeRegistry registry;
    RegisterAssetTypes(registry);

    std::vector<std::string> labels;
    for (const EditorAssetType& type : registry.Types())
    {
        if (type.Creation)
            labels.push_back(type.Creation->Label);
    }

    CHECK(labels == std::vector<std::string>{ "New Material", "New Scene", "New Shader" });
}

TEST_CASE("A mesh offers creating materials only for an .obj", "[asset types]")
{
    AssetTypeRegistry registry;
    RegisterAssetTypes(registry);

    const EditorAssetType& mesh = registry.Get(AssetType::Mesh);
    REQUIRE(mesh.Actions.size() == 1);

    CHECK(mesh.Actions[0].Shown("a.obj"));
    CHECK_FALSE(mesh.Actions[0].Shown("a.fbx"));
}

TEST_CASE("Scenes open in the editor, and shaders, textures and meshes open outside it", "[asset types]")
{
    AssetTypeRegistry registry;
    RegisterAssetTypes(registry);

    for (const EditorAssetType& type : registry.Types())
    {
        CHECK(static_cast<bool>(type.Open) == (type.Type == AssetType::Scene));

        const bool outside = type.Type == AssetType::Shader || type.Type == AssetType::Texture || type.Type == AssetType::Mesh;
        CHECK(static_cast<bool>(type.OpenFile) == outside);
    }
}
