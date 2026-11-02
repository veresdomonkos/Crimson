#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/asset_manager/meta_file.hpp>

#include "support/fake_gpu.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using crimson::tests::FakeGpuResourceManager;
using crimson::tests::TempFolder;

namespace fs = std::filesystem;

namespace
{
    constexpr std::string_view kMainMaterial = R"({ "shader": "engine/shaders/main.hlsl", "properties": { "Color": [0.1, 0.2, 0.3, 1.0] } })";
    constexpr std::string_view kTriangleObj = "v 0 0 0\nv 1 0 0\nv 0 2 0\nf 1 2 3\n";

    std::string MakeImage()
    {
        constexpr std::array<unsigned char, 58> kBytes = {
            'B', 'M', 58, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0,
            40, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 24, 0, 0, 0, 0, 0, 4, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            255, 0, 0, 0
        };
        return std::string(reinterpret_cast<const char*>(kBytes.data()), kBytes.size());
    }

    std::string ReadFile(const fs::path& file)
    {
        std::ifstream stream(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }

    struct Fixture
    {
        TempFolder Temp;
        FakeGpuResourceManager Gpu;
    };

    bool Contains(const std::vector<AssetID>& ids, AssetID id)
    {
        return std::ranges::find(ids, id) != ids.end();
    }
}

TEST_CASE_METHOD(Fixture, "Material data survives being written and read back", "[asset editing]")
{
    Temp.Write("image.bmp", MakeImage());
    Temp.Write("mine.mat", kMainMaterial);
    AssetManager assets(Gpu, Temp.Path());

    MaterialData data;
    data.Shader = assets.FindId("engine/shaders/main.hlsl");
    data.Settings = { .RenderMode = MaterialRenderMode::Transparent, .Cull = CullMode::Front };
    data.Properties["Single"] = { 0.5f };
    data.Properties["Color"] = { 0.25f, 0.5f, 0.75f, 1.0f };
    data.Textures["ColorMap"] = assets.FindId("image.bmp");

    const auto parsed = ParseMaterialData(WriteMaterialData(data, assets), assets, "test");

    REQUIRE(parsed);
    CHECK(*parsed == data);
}

TEST_CASE_METHOD(Fixture, "Materials written by hand keep working", "[asset editing]")
{
    Temp.Write("image.bmp", MakeImage());
    AssetManager assets(Gpu, Temp.Path());

    const auto data = ParseMaterialData(R"({
        "shader": "engine/shaders/main.hlsl",
        "properties": { "Number": 2, "Color": [1, 0, 0, 1], "TooLong": [1, 2, 3, 4, 5] },
        "textures": { "ColorMap": "image.bmp", "Gone": "no/such.bmp" }
    })", assets, "test");

    REQUIRE(data);
    CHECK(data->Shader == assets.FindId("engine/shaders/main.hlsl"));
    CHECK(data->Settings == MaterialSettings{});
    CHECK(data->Properties.at("Number") == std::vector{ 2.0f });
    CHECK_FALSE(data->Properties.contains("TooLong"));
    CHECK(data->Textures.size() == 1);
    CHECK(data->Textures.at("ColorMap") == assets.FindId("image.bmp"));
}

TEST_CASE_METHOD(Fixture, "A saved reference survives its file being renamed", "[asset editing]")
{
    Temp.Write("old.bmp", MakeImage());
    Temp.Write("mine.mat", kMainMaterial);
    AssetManager assets(Gpu, Temp.Path());

    const AssetID material = assets.FindId("mine.mat");
    const AssetID texture = assets.FindId("old.bmp");

    MaterialData data = *assets.GetMaterialData(material);
    data.Textures["ColorMap"] = texture;
    REQUIRE(assets.SaveMaterial(material, data));

    REQUIRE(assets.Rename(texture, "folder/new.bmp"));

    const auto read = assets.ReadMaterialFile(material);
    REQUIRE(read);
    CHECK(read->Textures.at("ColorMap") == texture);
}

TEST_CASE_METHOD(Fixture, "Refreshing finds what changed on disk", "[asset editing]")
{
    Temp.Write("stays.obj", kTriangleObj);
    const fs::path moved = Temp.Write("moved.obj", kTriangleObj);
    const fs::path gone = Temp.Write("gone.obj", kTriangleObj);

    AssetManager assets(Gpu, Temp.Path());

    const AssetID staysId = assets.FindId("stays.obj");
    const AssetID movedId = assets.FindId("moved.obj");
    const AssetID goneId = assets.FindId("gone.obj");

    SECTION("Nothing, when nothing did")
    {
        CHECK(assets.Refresh().empty());
        CHECK(assets.FindId("stays.obj") == staysId);
    }

    SECTION("A new file")
    {
        Temp.Write("new/file.obj", kTriangleObj);
        CHECK(assets.Refresh().empty());
        CHECK(assets.FindId("new/file.obj"));
    }

    SECTION("A file moved with its meta keeps its id")
    {
        fs::create_directories(Temp.Path() / "elsewhere");
        fs::rename(moved, Temp.Path() / "elsewhere/moved.obj");
        fs::rename(fs::path(moved) += ".meta", Temp.Path() / "elsewhere/moved.obj.meta");

        CHECK(assets.Refresh().empty());
        CHECK(assets.FindId("elsewhere/moved.obj") == movedId);
        CHECK_FALSE(assets.FindId("moved.obj"));
        CHECK(assets.GetInfo(movedId)->Path == "elsewhere/moved.obj");
    }

    SECTION("A deleted file is removed, and what it had loaded is freed")
    {
        REQUIRE(assets.LoadMesh(goneId));
        CHECK(Gpu.LiveVertexBuffers == 1);

        fs::remove(gone);

        const std::vector<AssetID> removed = assets.Refresh();

        CHECK(removed == std::vector{ goneId });
        CHECK(assets.GetInfo(goneId) == nullptr);
        CHECK_FALSE(assets.FindId("gone.obj"));
        CHECK(Gpu.LiveVertexBuffers == 0);
    }

    SECTION("A lost meta is written again with the same id")
    {
        fs::remove(fs::path(moved) += ".meta");

        CHECK(assets.Refresh().empty());
        CHECK(assets.FindId("moved.obj") == movedId);
        CHECK(fs::exists(fs::path(moved) += ".meta"));
    }

    SECTION("A copy with the same meta gets its own id")
    {
        fs::copy_file(moved, Temp.Path() / "copy.obj");
        fs::copy_file(fs::path(moved) += ".meta", Temp.Path() / "copy.obj.meta");

        CHECK(assets.Refresh().empty());
        CHECK(assets.FindId("copy.obj"));
        CHECK(assets.FindId("copy.obj") != movedId);
        CHECK(assets.FindId("moved.obj") == movedId);
    }

    SECTION("Engine assets stay")
    {
        CHECK(assets.Refresh().empty());
        CHECK(assets.FindId("engine/materials/default.mat"));
    }
}

TEST_CASE_METHOD(Fixture, "Removing what a material uses leaves the material safe", "[asset editing]")
{
    const fs::path textureFile = Temp.Write("image.bmp", MakeImage());
    const fs::path shaderFile = Temp.Write("own.hlsl", R"(
#include "crimson/bindings.hlsli"
float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }
float4 PSMain() : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }
)");
    Temp.Write("textured.mat", R"({ "shader": "engine/shaders/main.hlsl", "textures": { "ColorMap": "image.bmp" } })");
    Temp.Write("own.mat", R"({ "shader": "own.hlsl" })");

    AssetManager assets(Gpu, Temp.Path());

    const AssetID textured = assets.FindId("textured.mat");
    const AssetID own = assets.FindId("own.mat");

    const MaterialHandle texturedHandle = assets.LoadMaterial(textured);
    REQUIRE(texturedHandle);
    REQUIRE(assets.LoadMaterial(own));

    SECTION("A texture: the slot goes back to the default")
    {
        REQUIRE(Gpu.Materials.at(texturedHandle.GetRaw()).Textures.contains("ColorMap"));

        fs::remove(textureFile);
        assets.Refresh();

        CHECK_FALSE(Gpu.Materials.at(texturedHandle.GetRaw()).Textures.contains("ColorMap"));
        CHECK(assets.LoadMaterial(textured) == texturedHandle);
        CHECK(assets.GetMaterialData(textured)->Textures.empty());
        CHECK(Gpu.LiveTextures == 0);
    }

    SECTION("A shader: the material is unloaded before it")
    {
        CHECK(assets.Delete(assets.FindId("own.hlsl")));

        CHECK_FALSE(assets.IsLoaded(own));
        CHECK(Gpu.LiveMaterials == 1);
        CHECK(Gpu.LiveShaders == 1);

        const auto firstShader = std::ranges::find(Gpu.DestroyOrder, "shader");
        CHECK(firstShader != Gpu.DestroyOrder.end());
        CHECK(std::ranges::find(Gpu.DestroyOrder.begin(), firstShader, "material") != firstShader);

        // There is nothing left to load it from, and that isn't a crash
        CHECK_FALSE(assets.LoadMaterial(own));
    }
}

TEST_CASE_METHOD(Fixture, "New materials", "[asset editing]")
{
    Temp.Write("taken.mat", kMainMaterial);
    AssetManager assets(Gpu, Temp.Path());

    SECTION("Work right away, on the engine's shader")
    {
        const AssetID id = assets.CreateMaterial("folder/new.mat");

        REQUIRE(id);
        CHECK(fs::exists(Temp.Path() / "folder/new.mat"));
        CHECK(fs::exists(Temp.Path() / "folder/new.mat.meta"));
        CHECK(assets.GetInfo(id)->Type == AssetType::Material);
        CHECK(assets.LoadMaterial(id));
        CHECK(assets.GetMaterialData(id)->Shader == assets.FindId("engine/shaders/main.hlsl"));
    }

    SECTION("Refuse bad places")
    {
        const std::string before = ReadFile(Temp.Path() / "taken.mat");

        CHECK_FALSE(assets.CreateMaterial("taken.mat"));
        CHECK_FALSE(assets.CreateMaterial("engine/mine.mat"));
        CHECK_FALSE(assets.CreateMaterial("../outside.mat"));
        CHECK_FALSE(assets.CreateMaterial("no_extension"));

        CHECK(ReadFile(Temp.Path() / "taken.mat") == before);
        CHECK_FALSE(fs::exists(Temp.Path().parent_path() / "outside.mat"));
    }
}

TEST_CASE_METHOD(Fixture, "Renaming moves the file and its meta", "[asset editing]")
{
    Temp.Write("a.obj", kTriangleObj);
    Temp.Write("taken.obj", kTriangleObj);
    AssetManager assets(Gpu, Temp.Path());
    const AssetID id = assets.FindId("a.obj");

    SECTION("To a new folder")
    {
        REQUIRE(assets.Rename(id, "deep/down/b.obj"));

        CHECK(assets.FindId("deep/down/b.obj") == id);
        CHECK_FALSE(assets.FindId("a.obj"));
        CHECK(fs::exists(Temp.Path() / "deep/down/b.obj"));
        CHECK(fs::exists(Temp.Path() / "deep/down/b.obj.meta"));
        CHECK_FALSE(fs::exists(Temp.Path() / "a.obj"));
        CHECK_FALSE(fs::exists(Temp.Path() / "a.obj.meta"));
    }

    SECTION("It is the same asset to a new manager")
    {
        REQUIRE(assets.Rename(id, "b.obj"));
        CHECK(AssetManager(Gpu, Temp.Path()).FindId("b.obj") == id);
    }

    SECTION("Refuses what would break something")
    {
        CHECK_FALSE(assets.Rename(id, "taken.obj"));
        CHECK_FALSE(assets.Rename(id, "b.mat"));
        CHECK_FALSE(assets.Rename(id, "engine/b.obj"));
        CHECK_FALSE(assets.Rename(id, "../b.obj"));
        CHECK_FALSE(assets.Rename(id, ""));
        CHECK(assets.FindId("a.obj") == id);
    }
}

TEST_CASE_METHOD(Fixture, "Deleting removes the file, its meta and what it loaded", "[asset editing]")
{
    const fs::path file = Temp.Write("a.obj", kTriangleObj);
    AssetManager assets(Gpu, Temp.Path());
    const AssetID id = assets.FindId("a.obj");
    REQUIRE(assets.LoadMesh(id));

    REQUIRE(assets.Delete(id));

    CHECK_FALSE(fs::exists(file));
    CHECK_FALSE(fs::exists(fs::path(file) += ".meta"));
    CHECK(assets.GetInfo(id) == nullptr);
    CHECK(Gpu.LiveVertexBuffers == 0);
}

TEST_CASE_METHOD(Fixture, "Engine assets can't be changed", "[asset editing]")
{
    AssetManager assets(Gpu, Temp.Path());
    const AssetID id = assets.DefaultMaterial();

    REQUIRE(assets.GetInfo(id)->ReadOnly);
    CHECK(assets.GetFilePath(id).empty());
    CHECK_FALSE(assets.Rename(id, "mine.mat"));
    CHECK_FALSE(assets.Delete(id));
    CHECK_FALSE(assets.SaveMaterial(id, MaterialData{}));
    CHECK(assets.ApplyMaterial(id, MaterialData{}) == MaterialApplyResult::Failed);
    CHECK(assets.GetInfo(id));
}

TEST_CASE_METHOD(Fixture, "Applying a material shows it without touching the file", "[asset editing]")
{
    const fs::path file = Temp.Write("mine.mat", kMainMaterial);
    Temp.Write("image.bmp", MakeImage());
    Temp.Write("own.hlsl", R"(
#include "crimson/bindings.hlsli"
float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }
float4 PSMain() : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }
)");

    AssetManager assets(Gpu, Temp.Path());
    const AssetID id = assets.FindId("mine.mat");
    const MaterialHandle handle = assets.LoadMaterial(id);
    REQUIRE(handle);

    const std::string fileBefore = ReadFile(file);
    MaterialData data = *assets.GetMaterialData(id);

    SECTION("Settings and properties keep the handle")
    {
        data.Settings.Cull = CullMode::None;
        data.Properties["Color"] = { 1.0f, 0.0f, 0.0f, 1.0f };

        CHECK(assets.ApplyMaterial(id, data) == MaterialApplyResult::Updated);
        CHECK(assets.LoadMaterial(id) == handle);
        CHECK(Gpu.Materials.at(handle.GetRaw()).Settings.Cull == CullMode::None);
        CHECK(*assets.GetMaterialData(id) == data);
        CHECK(ReadFile(file) == fileBefore);
    }

    SECTION("Textures can be set and cleared")
    {
        data.Textures["ColorMap"] = assets.FindId("image.bmp");
        REQUIRE(assets.ApplyMaterial(id, data) == MaterialApplyResult::Updated);
        CHECK(Gpu.Materials.at(handle.GetRaw()).Textures.contains("ColorMap"));

        data.Textures.clear();
        REQUIRE(assets.ApplyMaterial(id, data) == MaterialApplyResult::Updated);
        CHECK_FALSE(Gpu.Materials.at(handle.GetRaw()).Textures.contains("ColorMap"));
    }

    SECTION("Another shader needs a new material")
    {
        data.Shader = assets.FindId("own.hlsl");

        CHECK(assets.ApplyMaterial(id, data) == MaterialApplyResult::Replaced);
        CHECK(assets.LoadMaterial(id) != handle);
        CHECK(Gpu.LiveMaterials == 1);
    }

    SECTION("A shader that can't load leaves the material as it was")
    {
        Temp.Write("broken.hlsl", "this does not compile");
        assets.Refresh();

        data.Shader = assets.FindId("broken.hlsl");

        CHECK(assets.ApplyMaterial(id, data) == MaterialApplyResult::Failed);
        CHECK(assets.LoadMaterial(id) == handle);
        CHECK(assets.GetMaterialData(id)->Shader == assets.FindId("engine/shaders/main.hlsl"));
    }

    SECTION("Reading the file again gets back what was saved")
    {
        data.Properties["Color"] = { 1.0f, 0.0f, 0.0f, 1.0f };
        REQUIRE(assets.ApplyMaterial(id, data) == MaterialApplyResult::Updated);

        const auto saved = assets.ReadMaterialFile(id);
        REQUIRE(saved);
        CHECK(saved->Properties.at("Color") != data.Properties.at("Color"));
    }

    SECTION("Saving is what a new manager sees")
    {
        data.Properties["Color"] = { 1.0f, 0.0f, 0.0f, 1.0f };
        REQUIRE(assets.SaveMaterial(id, data));

        AssetManager reopened(Gpu, Temp.Path());
        CHECK(reopened.GetMaterialData(id)->Properties.at("Color") == data.Properties.at("Color"));
    }
}

TEST_CASE_METHOD(Fixture, "Reimporting a texture", "[asset editing]")
{
    const fs::path file = Temp.Write("image.bmp", MakeImage());
    Temp.Write("mine.mat", R"({ "shader": "engine/shaders/main.hlsl", "textures": { "ColorMap": "image.bmp" } })");

    AssetManager assets(Gpu, Temp.Path());
    const AssetID id = assets.FindId("image.bmp");

    nlohmann::json settings = assets.GetInfo(id)->Settings;
    settings["flipY"] = true;

    SECTION("Before it is loaded, only stores the settings")
    {
        REQUIRE(assets.ReimportTexture(id, settings));

        CHECK(Gpu.LiveTextures == 0);
        CHECK(assets.GetInfo(id)->Settings.at("flipY") == true);
    }

    SECTION("Keeps the settings for next time")
    {
        REQUIRE(assets.ReimportTexture(id, settings));
        CHECK(AssetManager(Gpu, Temp.Path()).GetInfo(id)->Settings.at("flipY") == true);
    }

    SECTION("While loaded, replaces it for the materials using it")
    {
        const MaterialHandle material = assets.LoadMaterial("mine.mat");
        const TextureHandle before = assets.LoadTexture(id);
        REQUIRE(Gpu.Materials.at(material.GetRaw()).Textures.at("ColorMap") == before);

        REQUIRE(assets.ReimportTexture(id, settings));

        const TextureHandle after = assets.LoadTexture(id);
        CHECK(after != before);
        CHECK(Gpu.LiveTextures == 1);
        CHECK(Gpu.Materials.at(material.GetRaw()).Textures.at("ColorMap") == after);
    }

    SECTION("A file that can no longer be read keeps the old one")
    {
        const TextureHandle before = assets.LoadTexture(id);
        Temp.Write("image.bmp", "not an image");

        CHECK_FALSE(assets.ReimportTexture(id, settings));
        CHECK(assets.LoadTexture(id) == before);
    }

    SECTION("Only textures can be")
    {
        CHECK_FALSE(assets.ReimportTexture(assets.FindId("mine.mat"), settings));
    }
}

TEST_CASE_METHOD(Fixture, "Mipmaps follow the setting", "[asset editing]")
{
    Temp.Write("image.bmp", MakeImage());
    AssetManager assets(Gpu, Temp.Path());
    const AssetID id = assets.FindId("image.bmp");

    // New textures get them by default, and a 1x1 image can only have one level
    CHECK(assets.GetInfo(id)->Settings.at("generateMips") == true);
    REQUIRE(assets.LoadTexture(id));
    CHECK(Gpu.LastTextureInfo.MipLevels == 1);
}
