#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/asset_manager/slot_materials.hpp>
#include <crimson/asset_manager/meta_file.hpp>
#include <crimson/asset_manager/importers/material_asset.hpp>
#include <crimson/asset_manager/importers/mesh_asset.hpp>
#include <crimson/asset_manager/importers/scene_asset.hpp>
#include <crimson/asset_manager/importers/shader_asset.hpp>
#include <crimson/asset_manager/importers/texture_asset.hpp>


#include "support/fake_gpu.hpp"
#include "support/log_counter.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using crimson::tests::FakeGpuResourceManager;
using crimson::tests::LogCounter;
using crimson::tests::TempFolder;

namespace fs = std::filesystem;

namespace
{
    constexpr std::string_view kMainMaterial = R"({ "shader": "engine/shaders/S_Lit.hlsl", "properties": { "Color": [0.1, 0.2, 0.3, 1.0] } })";
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
    data.Shader = assets.Find<ShaderAsset>("engine/shaders/S_Lit.hlsl");
    data.Settings = { .RenderMode = MaterialRenderMode::Transparent, .Cull = CullMode::Front };
    data.Properties["Single"] = { 0.5f };
    data.Properties["Color"] = { 0.25f, 0.5f, 0.75f, 1.0f };
    data.Textures["ColorMap"] = assets.Find<TextureAsset>("image.bmp");

    const auto parsed = material_data::Parse(material_data::Write(data, assets), assets, "test");

    REQUIRE(parsed);
    CHECK(*parsed == data);
}

TEST_CASE_METHOD(Fixture, "Materials written by hand keep working", "[asset editing]")
{
    Temp.Write("image.bmp", MakeImage());
    AssetManager assets(Gpu, Temp.Path());

    const auto data = material_data::Parse(R"({
        "shader": "engine/shaders/S_Lit.hlsl",
        "properties": { "Number": 2, "Color": [1, 0, 0, 1], "TooLong": [1, 2, 3, 4, 5] },
        "textures": { "ColorMap": "image.bmp", "Gone": "no/such.bmp" }
    })", assets, "test");

    REQUIRE(data);
    CHECK(data->Shader == assets.Find<ShaderAsset>("engine/shaders/S_Lit.hlsl"));
    CHECK(data->Settings == MaterialSettings{});
    CHECK(data->Properties.at("Number") == std::vector{ 2.0f });
    CHECK_FALSE(data->Properties.contains("TooLong"));
    CHECK(data->Textures.at("ColorMap") == assets.Find<TextureAsset>("image.bmp"));

    // A texture that isn't there is kept, and written back as it was
    REQUIRE(data->Textures.contains("Gone"));
    CHECK(assets.IsMissing(data->Textures.at("Gone")));
    CHECK(material_data::Write(*data, assets).find(R"("Gone": "no/such.bmp")") != std::string::npos);
}

TEST_CASE_METHOD(Fixture, "A material keeps its references to missing files", "[asset editing]")
{
    const fs::path textureFile = Temp.Write("image.bmp", MakeImage());
    const fs::path shaderFile = Temp.Write("own.hlsl", R"(
#include "crimson/bindings.hlsli"
float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }
float4 PSMain() : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }
)");
    const fs::path materialFile = Temp.Write("mine.mat", R"({ "shader": "own.hlsl", "textures": { "ColorMap": "image.bmp" } })");

    TextureID texture;
    ShaderID shader;

    // Saved once, for the references to have their guids
    {
        AssetManager assets(Gpu, Temp.Path());
        texture = assets.Find<TextureAsset>("image.bmp");
        shader = assets.Find<ShaderAsset>("own.hlsl");

        const MaterialID material = assets.Find<MaterialAsset>("mine.mat");
        REQUIRE(assets.Save(material, *assets.GetData(material)));
    }

    const std::string saved = ReadFile(materialFile);

    // Moved away with their meta files, as if they were missing for a while
    std::vector<fs::path> moved;
    for (const fs::path& file : { textureFile, shaderFile })
    {
        moved.push_back(file);
        moved.push_back(meta::PathFor(file));
    }

    for (const fs::path& file : moved)
        fs::rename(file, fs::path(file) += ".away");

    {
        AssetManager assets(Gpu, Temp.Path());
        const MaterialID material = assets.Find<MaterialAsset>("mine.mat");

        const MaterialData* data = assets.GetData(material);
        REQUIRE(data);
        CHECK(data->Shader == shader);
        CHECK(data->Textures.at("ColorMap") == texture);
        CHECK(assets.IsMissing(shader));
        CHECK(assets.GetMissing(texture)->Path == "image.bmp");

        REQUIRE(assets.Save(material, *data));
        CHECK(ReadFile(materialFile) == saved);
    }

    for (const fs::path& file : moved)
        fs::rename(fs::path(file) += ".away", file);

    {
        AssetManager assets(Gpu, Temp.Path());
        const MaterialID material = assets.Find<MaterialAsset>("mine.mat");

        CHECK(assets.Get(material));
        CHECK(assets.GetData(material)->Textures.at("ColorMap") == texture);
        CHECK_FALSE(assets.IsMissing(texture));
    }
}

TEST_CASE_METHOD(Fixture, "A saved reference survives its file being renamed", "[asset editing]")
{
    Temp.Write("old.bmp", MakeImage());
    Temp.Write("mine.mat", kMainMaterial);
    AssetManager assets(Gpu, Temp.Path());

    const MaterialID material = assets.Find<MaterialAsset>("mine.mat");
    const TextureID texture = assets.Find<TextureAsset>("old.bmp");

    MaterialData data = *assets.GetData(material);
    data.Textures["ColorMap"] = texture;
    REQUIRE(assets.Save(material, data));

    REQUIRE(assets.Rename(texture, "folder/new.bmp"));

    const auto read = assets.Read(material);
    REQUIRE(read);
    CHECK(read->Textures.at("ColorMap") == texture);
}

TEST_CASE_METHOD(Fixture, "Moving a texture updates the materials referencing it", "[asset editing]")
{
    Temp.Write("old.bmp", MakeImage());
    const fs::path file = Temp.Write("mine.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl", "textures": { "ColorMap": "old.bmp" } })");
    AssetManager assets(Gpu, Temp.Path());

    const TextureID texture = assets.Find<TextureAsset>("old.bmp");

    SECTION("A reference by path alone")
    {
        REQUIRE(assets.Rename(texture, "folder/new.bmp"));

        const std::string text = ReadFile(file);
        CHECK(text.find("folder/new.bmp") != std::string::npos);
        CHECK(text.find(texture.ToString()) != std::string::npos);
        CHECK(text.find("old.bmp") == std::string::npos);

        AssetManager reopened(Gpu, Temp.Path());
        const auto read = reopened.Read(reopened.Find<MaterialAsset>("mine.mat"));
        REQUIRE(read);
        CHECK(read->Textures.at("ColorMap") == texture);
    }

    SECTION("A reference with an id gets the new path too")
    {
        const MaterialID material = assets.Find<MaterialAsset>("mine.mat");
        REQUIRE(assets.Save(material, *assets.GetData(material)));
        REQUIRE(ReadFile(file).find("\"old.bmp\"") != std::string::npos);

        REQUIRE(assets.Rename(texture, "folder/new.bmp"));

        const std::string text = ReadFile(file);
        CHECK(text.find("folder/new.bmp") != std::string::npos);
        CHECK(text.find("old.bmp") == std::string::npos);
    }

    SECTION("Moving it again follows it")
    {
        REQUIRE(assets.Rename(texture, "a.bmp"));
        REQUIRE(assets.Rename(texture, "b.bmp"));

        const std::string text = ReadFile(file);
        CHECK(text.find("b.bmp") != std::string::npos);
        CHECK(text.find("a.bmp") == std::string::npos);
    }
}

TEST_CASE_METHOD(Fixture, "Moving a shader updates the materials referencing it", "[asset editing]")
{
    Temp.Write("old.hlsl", "");
    const fs::path file = Temp.Write("mine.mat", R"({ "shader": "old.hlsl" })");
    AssetManager assets(Gpu, Temp.Path());

    const ShaderID shader = assets.Find<ShaderAsset>("old.hlsl");
    REQUIRE(assets.Rename(shader, "shaders/new.hlsl"));

    AssetManager reopened(Gpu, Temp.Path());
    const auto read = reopened.Read(reopened.Find<MaterialAsset>("mine.mat"));
    REQUIRE(read);
    CHECK(read->Shader == shader);
}

TEST_CASE_METHOD(Fixture, "A renamed folder keeps the references inside it working", "[asset editing]")
{
    Temp.Write("sponza/textures/arch.bmp", MakeImage());
    Temp.Write("sponza/materials/arch.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl", "textures": { "ColorMap": "sponza/textures/arch.bmp" } })");
    AssetManager assets(Gpu, Temp.Path());

    const TextureID texture = assets.Find<TextureAsset>("sponza/textures/arch.bmp");

    fs::rename(Temp.Path() / "sponza", Temp.Path() / "renamed");
    CHECK(assets.Refresh().empty());

    AssetManager reopened(Gpu, Temp.Path());
    const auto read = reopened.Read(reopened.Find<MaterialAsset>("renamed/materials/arch.mat"));
    REQUIRE(read);
    CHECK(read->Textures.at("ColorMap") == texture);
}

TEST_CASE_METHOD(Fixture, "A file moved outside the editor updates the materials referencing it", "[asset editing]")
{
    const fs::path image = Temp.Write("old.bmp", MakeImage());
    Temp.Write("mine.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl", "textures": { "ColorMap": "old.bmp" } })");
    AssetManager assets(Gpu, Temp.Path());

    const TextureID texture = assets.Find<TextureAsset>("old.bmp");

    fs::create_directories(Temp.Path() / "folder");
    fs::rename(image, Temp.Path() / "folder/new.bmp");
    fs::rename(meta::PathFor(image), meta::PathFor(Temp.Path() / "folder/new.bmp"));
    CHECK(assets.Refresh().empty());

    AssetManager reopened(Gpu, Temp.Path());
    const auto read = reopened.Read(reopened.Find<MaterialAsset>("mine.mat"));
    REQUIRE(read);
    CHECK(read->Textures.at("ColorMap") == texture);
}

TEST_CASE_METHOD(Fixture, "Moving a file leaves everything else in materials alone", "[asset editing]")
{
    Temp.Write("old.bmp", MakeImage());
    Temp.Write("other.bmp", MakeImage());

    SECTION("Files with no reference to it stay exactly as they were")
    {
        constexpr std::string_view kOther = R"({ "shader": "engine/shaders/S_Lit.hlsl", "textures": { "ColorMap": "other.bmp" } })";
        constexpr std::string_view kBroken = "{ not json";

        const fs::path other = Temp.Write("other.mat", kOther);
        const fs::path broken = Temp.Write("broken.mat", kBroken);
        AssetManager assets(Gpu, Temp.Path());

        REQUIRE(assets.Rename(assets.Find<TextureAsset>("old.bmp"), "new.bmp"));

        CHECK(ReadFile(other) == kOther);
        CHECK(ReadFile(broken) == kBroken);
    }

    SECTION("A rewritten file only changes the reference")
    {
        const fs::path file = Temp.Write("mine.mat", R"({
            "shader": "engine/shaders/S_Lit.hlsl",
            "properties": { "Roughness": 0.8 },
            "textures": { "ColorMap": "old.bmp", "NormalMap": "other.bmp", "Gone": "no/such.bmp" }
        })");
        AssetManager assets(Gpu, Temp.Path());

        REQUIRE(assets.Rename(assets.Find<TextureAsset>("old.bmp"), "new.bmp"));

        const std::string text = ReadFile(file);
        CHECK(text.find("new.bmp") != std::string::npos);
        CHECK(text.find("Roughness") != std::string::npos);
        CHECK(text.find("\"engine/shaders/S_Lit.hlsl\"") != std::string::npos);
        CHECK(text.find("\"other.bmp\"") != std::string::npos);
        CHECK(text.find("no/such.bmp") != std::string::npos);
        CHECK(text.find("renderMode") == std::string::npos);
        CHECK(text.find("cull") == std::string::npos);
    }

    SECTION("Unsaved changes stay unsaved")
    {
        Temp.Write("mine.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl", "properties": { "Roughness": 0.8 }, "textures": { "ColorMap": "old.bmp" } })");
        AssetManager assets(Gpu, Temp.Path());
        const MaterialID material = assets.Find<MaterialAsset>("mine.mat");

        REQUIRE(assets.Get(material));

        MaterialData edited = *assets.GetData(material);
        edited.Properties["Roughness"] = { 0.2f };
        REQUIRE(assets.Apply(material, edited) != AssetUpdate::Failed);

        REQUIRE(assets.Rename(assets.Find<TextureAsset>("old.bmp"), "new.bmp"));

        CHECK(*assets.GetData(material) == edited);

        const auto read = assets.Read(material);
        REQUIRE(read);
        CHECK(read->Properties.at("Roughness") == std::vector{ 0.8f });
    }
}

TEST_CASE_METHOD(Fixture, "Refreshing finds what changed on disk", "[asset editing]")
{
    Temp.Write("stays.obj", kTriangleObj);
    const fs::path moved = Temp.Write("moved.obj", kTriangleObj);
    const fs::path gone = Temp.Write("gone.obj", kTriangleObj);

    AssetManager assets(Gpu, Temp.Path());

    const MeshID staysId = assets.Find<MeshAsset>("stays.obj");
    const MeshID movedId = assets.Find<MeshAsset>("moved.obj");
    const MeshID goneId = assets.Find<MeshAsset>("gone.obj");

    SECTION("Nothing, when nothing did")
    {
        CHECK(assets.Refresh().empty());
        CHECK(assets.Find<MeshAsset>("stays.obj") == staysId);
    }

    SECTION("A new file")
    {
        Temp.Write("new/file.obj", kTriangleObj);
        CHECK(assets.Refresh().empty());
        CHECK(assets.Find<MeshAsset>("new/file.obj"));
    }

    SECTION("A file moved with its meta keeps its id")
    {
        fs::create_directories(Temp.Path() / "elsewhere");
        fs::rename(moved, Temp.Path() / "elsewhere/moved.obj");
        fs::rename(fs::path(moved) += ".meta", Temp.Path() / "elsewhere/moved.obj.meta");

        CHECK(assets.Refresh().empty());
        CHECK(assets.Find<MeshAsset>("elsewhere/moved.obj") == movedId);
        CHECK_FALSE(assets.Find<MeshAsset>("moved.obj"));
        CHECK(assets.GetInfo(movedId)->Path == "elsewhere/moved.obj");
    }

    SECTION("A deleted file is removed, and what it had loaded is freed")
    {
        REQUIRE(assets.Get(goneId));
        CHECK(Gpu.LiveVertexBuffers == 1);

        fs::remove(gone);

        const std::vector<AssetID> removed = assets.Refresh();

        CHECK(removed == std::vector<AssetID>{ goneId });
        CHECK(assets.GetInfo(goneId) == nullptr);
        CHECK_FALSE(assets.Find<MeshAsset>("gone.obj"));
        CHECK(Gpu.LiveVertexBuffers == 0);
    }

    SECTION("A lost meta is written again with the same id")
    {
        fs::remove(fs::path(moved) += ".meta");

        CHECK(assets.Refresh().empty());
        CHECK(assets.Find<MeshAsset>("moved.obj") == movedId);
        CHECK(fs::exists(fs::path(moved) += ".meta"));
    }

    SECTION("A copy with the same meta gets its own id")
    {
        fs::copy_file(moved, Temp.Path() / "copy.obj");
        fs::copy_file(fs::path(moved) += ".meta", Temp.Path() / "copy.obj.meta");

        CHECK(assets.Refresh().empty());
        CHECK(assets.Find<MeshAsset>("copy.obj"));
        CHECK(assets.Find<MeshAsset>("copy.obj") != movedId);
        CHECK(assets.Find<MeshAsset>("moved.obj") == movedId);
    }

    SECTION("Engine assets stay")
    {
        CHECK(assets.Refresh().empty());
        CHECK(assets.Find<MaterialAsset>("engine/materials/M_Default.mat"));
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
    Temp.Write("textured.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl", "textures": { "ColorMap": "image.bmp" } })");
    Temp.Write("own.mat", R"({ "shader": "own.hlsl" })");

    AssetManager assets(Gpu, Temp.Path());

    const MaterialID textured = assets.Find<MaterialAsset>("textured.mat");
    const MaterialID own = assets.Find<MaterialAsset>("own.mat");

    const MaterialHandle texturedHandle = *assets.Get(textured);
    REQUIRE(texturedHandle);
    REQUIRE(assets.Get(own));

    SECTION("A texture: the slot goes back to the default, and the material keeps the reference")
    {
        REQUIRE(Gpu.Materials.at(texturedHandle.GetRaw()).Textures.contains("ColorMap"));
        const AssetID texture = assets.FindId("image.bmp");

        fs::remove(textureFile);
        assets.Refresh();

        CHECK_FALSE(Gpu.Materials.at(texturedHandle.GetRaw()).Textures.contains("ColorMap"));
        CHECK(*assets.Get(textured) == texturedHandle);
        CHECK(assets.GetData(textured)->Textures.at("ColorMap") == texture);
        CHECK(assets.IsMissing(texture));
        CHECK(Gpu.LiveTextures == 1); // only the flat normal the shader's empty slot uses

        // Back, next to its meta file, it is the same asset again
        Temp.Write("image.bmp", MakeImage());
        assets.Refresh();
        CHECK_FALSE(assets.IsMissing(texture));
    }

    SECTION("A shader: the material is unloaded before it")
    {
        CHECK(assets.Delete(assets.Find<ShaderAsset>("own.hlsl")));

        CHECK_FALSE(assets.IsLoaded(own));
        CHECK(Gpu.LiveMaterials == 1);
        CHECK(Gpu.LiveShaders == 1);

        const auto firstShader = std::ranges::find(Gpu.DestroyOrder, "shader");
        CHECK(firstShader != Gpu.DestroyOrder.end());
        CHECK(std::ranges::find(Gpu.DestroyOrder.begin(), firstShader, "material") != firstShader);

        // There is nothing left to load it from, and that isn't a crash
        CHECK_FALSE(assets.Get(own));
    }
}

TEST_CASE_METHOD(Fixture, "New materials", "[asset editing]")
{
    Temp.Write("taken.mat", kMainMaterial);
    AssetManager assets(Gpu, Temp.Path());

    SECTION("Work right away, on the engine's shader")
    {
        const MaterialID id = assets.Create<MaterialAsset>("folder/new.mat");

        REQUIRE(id);
        CHECK(fs::exists(Temp.Path() / "folder/new.mat"));
        CHECK(fs::exists(Temp.Path() / "folder/new.mat.meta"));
        CHECK(assets.GetInfo(id)->Type.Is<MaterialAsset>());
        CHECK(assets.Get(id));
        CHECK(assets.GetData(id)->Shader == assets.Find<ShaderAsset>("engine/shaders/S_Lit.hlsl"));
    }

    SECTION("Refuse bad places")
    {
        const std::string before = ReadFile(Temp.Path() / "taken.mat");

        CHECK_FALSE(assets.Create<MaterialAsset>("taken.mat"));
        CHECK_FALSE(assets.Create<MaterialAsset>("engine/mine.mat"));
        CHECK_FALSE(assets.Create<MaterialAsset>("../outside.mat"));
        CHECK_FALSE(assets.Create<MaterialAsset>("no_extension"));

        CHECK(ReadFile(Temp.Path() / "taken.mat") == before);
        CHECK_FALSE(fs::exists(Temp.Path().parent_path() / "outside.mat"));
    }
}

TEST_CASE_METHOD(Fixture, "New shaders", "[asset editing]")
{
    Temp.Write("taken.hlsl", "// mine");
    AssetManager assets(Gpu, Temp.Path());

    SECTION("Compile right away, with properties to set")
    {
        const ShaderID id = assets.Create<ShaderAsset>("folder/new.hlsl");

        REQUIRE(id);
        CHECK(fs::exists(Temp.Path() / "folder/new.hlsl.meta"));
        CHECK(assets.GetInfo(id)->Type.Is<ShaderAsset>());

        const ShaderHandle shader = *assets.Get(id);
        REQUIRE(shader);

        const ShaderInterface& shaderInterface = assets.GetShaderInterface(shader);
        CHECK(shaderInterface.Properties.contains("Color"));
        CHECK(shaderInterface.Properties.contains("Roughness"));
        CHECK(shaderInterface.Textures.contains("ColorMap"));
    }

    SECTION("Refuse bad places")
    {
        CHECK_FALSE(assets.Create<ShaderAsset>("taken.hlsl"));
        CHECK_FALSE(assets.Create<ShaderAsset>("engine/mine.hlsl"));
        CHECK_FALSE(assets.Create<ShaderAsset>("wrong.mat"));

        CHECK(ReadFile(Temp.Path() / "taken.hlsl") == "// mine");
    }
}

TEST_CASE_METHOD(Fixture, "Only some types can be made new", "[asset editing]")
{
    AssetManager assets(Gpu, Temp.Path());

    // A texture has no starting contents
    const LogCounter errors(LogLevel::Error, "can't be made without contents");
    CHECK_FALSE(assets.Create<TextureAsset>("a.png"));
    CHECK(errors.Count() == 1);
    CHECK_FALSE(fs::exists(Temp.Path() / "a.png"));

    // The extension has to be one of the type
    CHECK_FALSE(assets.Create<SceneAsset>("x.mat"));
    CHECK_FALSE(fs::exists(Temp.Path() / "x.mat"));
}

TEST_CASE_METHOD(Fixture, "A save that fails leaves the file as it was", "[asset editing]")
{
    Temp.Write("mine.mat", kMainMaterial);
    Temp.Write("level.scene", R"({ "version": 1, "entities": [ { "name": "Old" } ] })");

    // A folder in the temporary file's place makes the write fail
    fs::create_directories(Temp.Path() / "mine.mat.saving");
    fs::create_directories(Temp.Path() / "level.scene.saving");

    AssetManager assets(Gpu, Temp.Path());
    const std::string materialBefore = ReadFile(Temp.Path() / "mine.mat");
    const std::string sceneBefore = ReadFile(Temp.Path() / "level.scene");

    const LogCounter errors(LogLevel::Error, "Can't write");

    SECTION("A material")
    {
        const MaterialID id = assets.Find<MaterialAsset>("mine.mat");
        CHECK_FALSE(assets.Save(id, MaterialData{}));
        CHECK(errors.Count() == 1);
        CHECK(ReadFile(Temp.Path() / "mine.mat") == materialBefore);
    }

    SECTION("A scene")
    {
        const SceneID id = assets.Find<SceneAsset>("level.scene");
        CHECK_FALSE(assets.Save(id, SceneData{}));
        CHECK(errors.Count() == 1);
        CHECK(ReadFile(Temp.Path() / "level.scene") == sceneBefore);
    }
}

TEST_CASE_METHOD(Fixture, "Saving leaves no temporary file", "[asset editing]")
{
    Temp.Write("mine.mat", kMainMaterial);
    AssetManager assets(Gpu, Temp.Path());
    const MaterialID id = assets.Find<MaterialAsset>("mine.mat");

    MaterialData data = *assets.GetData(id);
    data.Properties["Color"] = { 1.0f, 0.0f, 0.0f, 1.0f };

    REQUIRE(assets.Save(id, data));
    CHECK_FALSE(fs::exists(Temp.Path() / "mine.mat.saving"));
    CHECK(assets.Read(id)->Properties.at("Color") == data.Properties.at("Color"));
}

TEST_CASE_METHOD(Fixture, "A reimported mesh is looked up in place with its new buffers", "[asset editing]")
{
    Temp.Write("a.obj", kTriangleObj);
    AssetManager assets(Gpu, Temp.Path());
    const MeshID mesh = assets.Find<MeshAsset>("a.obj");

    const Mesh* before = assets.Get(mesh);
    REQUIRE(before);
    const VertexBufferHandle oldBuffer = before->VertexBuffer;

    nlohmann::json settings = assets.GetInfo(mesh)->Settings;
    settings["flipV"] = !settings.value("flipV", true);
    CHECK(assets.Reimport(mesh, settings));

    CHECK(assets.Get(mesh) == before);
    CHECK(assets.Get(mesh)->VertexBuffer != oldBuffer);
}

TEST_CASE_METHOD(Fixture, "Renaming moves the file and its meta", "[asset editing]")
{
    Temp.Write("a.obj", kTriangleObj);
    Temp.Write("taken.obj", kTriangleObj);
    AssetManager assets(Gpu, Temp.Path());
    const MeshID id = assets.Find<MeshAsset>("a.obj");

    SECTION("To a new folder")
    {
        REQUIRE(assets.Rename(id, "deep/down/b.obj"));

        CHECK(assets.Find<MeshAsset>("deep/down/b.obj") == id);
        CHECK_FALSE(assets.Find<MeshAsset>("a.obj"));
        CHECK(fs::exists(Temp.Path() / "deep/down/b.obj"));
        CHECK(fs::exists(Temp.Path() / "deep/down/b.obj.meta"));
        CHECK_FALSE(fs::exists(Temp.Path() / "a.obj"));
        CHECK_FALSE(fs::exists(Temp.Path() / "a.obj.meta"));
    }

    SECTION("It is the same asset to a new manager")
    {
        REQUIRE(assets.Rename(id, "b.obj"));
        CHECK(AssetManager(Gpu, Temp.Path()).Find<MeshAsset>("b.obj") == id);
    }

    SECTION("Refuses what would break something")
    {
        CHECK_FALSE(assets.Rename(id, "taken.obj"));
        CHECK_FALSE(assets.Rename(id, "b.mat"));
        CHECK_FALSE(assets.Rename(id, "engine/b.obj"));
        CHECK_FALSE(assets.Rename(id, "../b.obj"));
        CHECK_FALSE(assets.Rename(id, ""));
        CHECK(assets.Find<MeshAsset>("a.obj") == id);
    }
}

TEST_CASE_METHOD(Fixture, "Deleting removes the file, its meta and what it loaded", "[asset editing]")
{
    const fs::path file = Temp.Write("a.obj", kTriangleObj);
    AssetManager assets(Gpu, Temp.Path());
    const MeshID id = assets.Find<MeshAsset>("a.obj");
    REQUIRE(assets.Get(id));

    REQUIRE(assets.Delete(id));

    CHECK_FALSE(fs::exists(file));
    CHECK_FALSE(fs::exists(fs::path(file) += ".meta"));
    CHECK(assets.GetInfo(id) == nullptr);
    CHECK(Gpu.LiveVertexBuffers == 0);
}

TEST_CASE_METHOD(Fixture, "Engine assets can't be changed", "[asset editing]")
{
    AssetManager assets(Gpu, Temp.Path());
    const MaterialID id = slot_materials::DefaultMaterial(assets);

    REQUIRE(assets.GetInfo(id)->ReadOnly);
    CHECK(assets.GetFilePath(id).empty());
    CHECK_FALSE(assets.Rename(id, "mine.mat"));
    CHECK_FALSE(assets.Delete(id));
    CHECK_FALSE(assets.Save(id, MaterialData{}));
    CHECK(assets.Apply(id, MaterialData{}) == AssetUpdate::Failed);
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
    const MaterialID id = assets.Find<MaterialAsset>("mine.mat");
    const MaterialHandle handle = *assets.Get(id);
    REQUIRE(handle);

    const std::string fileBefore = ReadFile(file);
    MaterialData data = *assets.GetData(id);

    SECTION("Settings and properties keep the handle")
    {
        data.Settings.Cull = CullMode::None;
        data.Properties["Color"] = { 1.0f, 0.0f, 0.0f, 1.0f };

        CHECK(assets.Apply(id, data) == AssetUpdate::Updated);
        CHECK(*assets.Get(id) == handle);
        CHECK(Gpu.Materials.at(handle.GetRaw()).Settings.Cull == CullMode::None);
        CHECK(*assets.GetData(id) == data);
        CHECK(ReadFile(file) == fileBefore);
    }

    SECTION("A color dragged every frame never rebuilds the material")
    {
        const int liveMaterials = Gpu.LiveMaterials;
        const auto destroyedMaterials = std::ranges::count(Gpu.DestroyOrder, "material");

        for (int i = 0; i < 10; ++i)
        {
            data.Properties["Color"] = { 0.1f * static_cast<float>(i), 0.0f, 0.0f, 1.0f };
            REQUIRE(assets.Apply(id, data) == AssetUpdate::Updated);
        }

        CHECK(*assets.Get(id) == handle);
        CHECK(Gpu.LiveMaterials == liveMaterials);
        CHECK(std::ranges::count(Gpu.DestroyOrder, "material") == destroyedMaterials);
    }

    SECTION("Textures can be set and cleared")
    {
        data.Textures["ColorMap"] = assets.Find<TextureAsset>("image.bmp");
        REQUIRE(assets.Apply(id, data) == AssetUpdate::Updated);
        CHECK(Gpu.Materials.at(handle.GetRaw()).Textures.contains("ColorMap"));

        data.Textures.clear();
        REQUIRE(assets.Apply(id, data) == AssetUpdate::Updated);
        CHECK_FALSE(Gpu.Materials.at(handle.GetRaw()).Textures.contains("ColorMap"));
    }

    SECTION("Another shader needs a new material")
    {
        data.Shader = assets.Find<ShaderAsset>("own.hlsl");

        CHECK(assets.Apply(id, data) == AssetUpdate::Replaced);
        CHECK(*assets.Get(id) != handle);
        CHECK(Gpu.LiveMaterials == 1);
    }

    SECTION("A shader that can't load leaves the material as it was")
    {
        Temp.Write("broken.hlsl", "this does not compile");
        assets.Refresh();

        data.Shader = assets.Find<ShaderAsset>("broken.hlsl");

        CHECK(assets.Apply(id, data) == AssetUpdate::Failed);
        CHECK(*assets.Get(id) == handle);
        CHECK(assets.GetData(id)->Shader == assets.Find<ShaderAsset>("engine/shaders/S_Lit.hlsl"));
    }

    SECTION("Reading the file again gets back what was saved")
    {
        data.Properties["Color"] = { 1.0f, 0.0f, 0.0f, 1.0f };
        REQUIRE(assets.Apply(id, data) == AssetUpdate::Updated);

        const auto saved = assets.Read(id);
        REQUIRE(saved);
        CHECK(saved->Properties.at("Color") != data.Properties.at("Color"));
    }

    SECTION("Saving is what a new manager sees")
    {
        data.Properties["Color"] = { 1.0f, 0.0f, 0.0f, 1.0f };
        REQUIRE(assets.Save(id, data));

        AssetManager reopened(Gpu, Temp.Path());
        CHECK(reopened.GetData(id)->Properties.at("Color") == data.Properties.at("Color"));
    }
}

TEST_CASE_METHOD(Fixture, "Reimporting a texture", "[asset editing]")
{
    const fs::path file = Temp.Write("image.bmp", MakeImage());
    Temp.Write("mine.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl", "textures": { "ColorMap": "image.bmp" } })");

    AssetManager assets(Gpu, Temp.Path());
    const TextureID id = assets.Find<TextureAsset>("image.bmp");

    nlohmann::json settings = assets.GetInfo(id)->Settings;
    settings["flipY"] = true;

    SECTION("Before it is loaded, only stores the settings")
    {
        REQUIRE(assets.Reimport(id, settings));

        CHECK(Gpu.LiveTextures == 0);
        CHECK(assets.GetInfo(id)->Settings.at("flipY") == true);
    }

    SECTION("Keeps the settings for next time")
    {
        REQUIRE(assets.Reimport(id, settings));
        CHECK(AssetManager(Gpu, Temp.Path()).GetInfo(id)->Settings.at("flipY") == true);
    }

    SECTION("While loaded, replaces it for the materials using it")
    {
        const MaterialHandle material = *assets.Get<MaterialAsset>("mine.mat");
        const TextureHandle before = *assets.Get(id);
        REQUIRE(Gpu.Materials.at(material.GetRaw()).Textures.at("ColorMap") == before);

        REQUIRE(assets.Reimport(id, settings));

        const TextureHandle after = *assets.Get(id);
        CHECK(after != before);
        CHECK(Gpu.LiveTextures == 2); // with the flat normal the shader's empty slot uses
        CHECK(Gpu.Materials.at(material.GetRaw()).Textures.at("ColorMap") == after);
        CHECK(Gpu.TexturesFreedWhileUsed == 0); // the swap came before the free
    }

    SECTION("A file that can no longer be read keeps the old one")
    {
        const TextureHandle before = *assets.Get(id);
        Temp.Write("image.bmp", "not an image");

        CHECK_FALSE(assets.Reimport(id, settings));
        CHECK(*assets.Get(id) == before);
    }
}

TEST_CASE_METHOD(Fixture, "Mipmaps follow the setting", "[asset editing]")
{
    Temp.Write("image.bmp", MakeImage());
    AssetManager assets(Gpu, Temp.Path());
    const TextureID id = assets.Find<TextureAsset>("image.bmp");

    // New textures get them by default, and a 1x1 image can only have one level
    CHECK(assets.GetInfo(id)->Settings.at("generateMips") == true);
    REQUIRE(assets.Get(id));
    CHECK(Gpu.LastTextureInfo.MipLevels == 1);
}

TEST_CASE("A material starts from what its shader declares", "[asset editing]")
{
    const TempFolder temp;
    temp.Write("image.bmp", MakeImage());
    temp.Write("annotated.hlsl", R"(
#include "crimson/bindings.hlsli"

cbuffer MaterialBlock : register(b0, space1)
{
    float Amount; // @default 0.25
    float Other; // @default 0.75
};

Texture2D Bumps : register(t0, space1); // @default normal

float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }
float4 PSMain() : SV_Target { return Amount * Other * Bumps.Sample(LinearWrap, float2(0, 0)); }
)");
    temp.Write("mine.mat", R"({ "shader": "annotated.hlsl", "properties": { "Other": 0.5 } })");

    FakeGpuResourceManager gpu;

    {
        AssetManager assets(gpu, temp.Path());
        const MaterialID id = assets.Find<MaterialAsset>("mine.mat");
        const MaterialHandle handle = *assets.Get(id);
        REQUIRE(handle);

        const auto propertyOf = [&](const char* name)
        {
            const std::vector<std::byte>& bytes = gpu.Materials.at(handle.GetRaw()).Properties.at(name);
            REQUIRE(bytes.size() == sizeof(float));

            float value = 0.0f;
            std::memcpy(&value, bytes.data(), sizeof(float));
            return value;
        };

        CHECK(propertyOf("Amount") == 0.25f);
        CHECK(propertyOf("Other") == 0.5f);

        const TextureHandle flatNormal = gpu.Materials.at(handle.GetRaw()).Textures.at("Bumps");
        CHECK(flatNormal);

        SECTION("Assigning a texture and clearing it again goes back to the flat normal")
        {
            MaterialData data = *assets.GetData(id);
            data.Textures["Bumps"] = assets.Find<TextureAsset>("image.bmp");
            REQUIRE(assets.Apply(id, data) == AssetUpdate::Updated);
            CHECK(gpu.Materials.at(handle.GetRaw()).Textures.at("Bumps") != flatNormal);

            data.Textures.clear();
            REQUIRE(assets.Apply(id, data) == AssetUpdate::Updated);
            CHECK(gpu.Materials.at(handle.GetRaw()).Textures.at("Bumps") == flatNormal);
        }

        SECTION("Removing a property from the data goes back to its default")
        {
            MaterialData data = *assets.GetData(id);
            data.Properties.erase("Other");
            REQUIRE(assets.Apply(id, data) == AssetUpdate::Updated);
            CHECK(propertyOf("Other") == 0.75f);
        }

        SECTION("Deleting the assigned texture goes back to the flat normal too")
        {
            MaterialData data = *assets.GetData(id);
            data.Textures["Bumps"] = assets.Find<TextureAsset>("image.bmp");
            REQUIRE(assets.Apply(id, data) == AssetUpdate::Updated);

            REQUIRE(assets.Delete(assets.Find<TextureAsset>("image.bmp")));
            CHECK(gpu.Materials.at(handle.GetRaw()).Textures.at("Bumps") == flatNormal);
        }
    }

    CHECK(gpu.NothingAlive());
}

constexpr std::string_view kShaderHeader = R"(
#include "crimson/bindings.hlsli"
#include "helper.hlsli"

cbuffer MaterialBlock : register(b0, space1)
{
    float Amount; // @default 0.25
)";

constexpr std::string_view kShaderFooter = R"(
};

float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }
float4 PSMain() : SV_Target { return Amount * HELPER_VALUE; }
)";

// The file times can be too coarse to tell two quick saves apart, so the change is made to look later.
// Saves in a row have to be pushed different amounts, or they end up at the same time again.
static void Touch(const fs::path& file, std::chrono::seconds ahead = std::chrono::seconds(2))
{
    fs::last_write_time(file, fs::last_write_time(file) + ahead);
}

TEST_CASE_METHOD(Fixture, "Changed shaders are reloaded along with their materials", "[asset editing]")
{
    const fs::path shaderFile = Temp.Write("mine.hlsl", std::string(kShaderHeader) + std::string(kShaderFooter));
    const fs::path helperFile = Temp.Write("helper.hlsli", "#define HELPER_VALUE 1.0\n");
    Temp.Write("mine.mat", R"({ "shader": "mine.hlsl", "properties": { "Amount": 0.5 } })");
    Temp.Write("unused.hlsl", std::string(kShaderHeader) + std::string(kShaderFooter));

    {
        AssetManager assets(Gpu, Temp.Path());

        const ShaderID shader = assets.Find<ShaderAsset>("mine.hlsl");
        const MaterialID material = assets.Find<MaterialAsset>("mine.mat");

        const MaterialHandle before = *assets.Get(material);
        const ShaderHandle shaderBefore = *assets.Get(shader);
        REQUIRE(before);

        SECTION("Nothing changed, nothing happens")
        {
            CHECK(assets.ReloadChanged().empty());
            CHECK(*assets.Get(material) == before);
        }

        SECTION("Editing the shader recreates its materials, keeping their values")
        {
            Temp.Write("mine.hlsl", std::string(kShaderHeader) + "    float Extra; // @default 3\n" + std::string(kShaderFooter));
            Touch(shaderFile);

            const std::vector<AssetID> reloaded = assets.ReloadChanged();
            CHECK(reloaded.size() == 2);
            CHECK(Contains(reloaded, shader));
            CHECK(Contains(reloaded, material));

            const ShaderHandle shaderAfter = *assets.Get(shader);
            const MaterialHandle after = *assets.Get(material);
            CHECK(shaderAfter != shaderBefore);
            CHECK(after != before);
            CHECK(Gpu.Materials.at(after.GetRaw()).Shader == shaderAfter);
            CHECK(assets.GetShaderInterface(shaderAfter).Properties.contains("Extra"));
            CHECK(Gpu.ShadersFreedWhileUsed == 0);
            CHECK(Gpu.LiveShaders == 1);
            CHECK(Gpu.LiveMaterials == 1);

            const std::vector<std::byte>& amount = Gpu.Materials.at(after.GetRaw()).Properties.at("Amount");
            float value = 0.0f;
            std::memcpy(&value, amount.data(), sizeof(float));
            CHECK(value == 0.5f);

            CHECK(assets.ReloadChanged().empty());
        }

        SECTION("Unsaved material edits survive it")
        {
            MaterialData data = *assets.GetData(material);
            data.Properties["Amount"] = { 0.125f };
            REQUIRE(assets.Apply(material, data) == AssetUpdate::Updated);

            Touch(shaderFile);
            REQUIRE_FALSE(assets.ReloadChanged().empty());

            CHECK(assets.GetData(material)->Properties.at("Amount") == std::vector{ 0.125f });
        }

        SECTION("Editing a file it includes counts too")
        {
            Temp.Write("helper.hlsli", "#define HELPER_VALUE 2.0\n");
            Touch(helperFile);

            CHECK(Contains(assets.ReloadChanged(), shader));
        }

        SECTION("A broken edit keeps the old version, and fixing it reloads")
        {
            Temp.Write("mine.hlsl", "this does not compile");
            Touch(shaderFile);

            CHECK(assets.ReloadChanged().empty());
            CHECK(*assets.Get(shader) == shaderBefore);
            CHECK(*assets.Get(material) == before);

            // Not tried again until it changes again
            CHECK(assets.ReloadChanged().empty());

            Temp.Write("mine.hlsl", std::string(kShaderHeader) + std::string(kShaderFooter));
            Touch(shaderFile, std::chrono::seconds(4));

            CHECK(Contains(assets.ReloadChanged(), material));
        }
    }

    CHECK(Gpu.NothingAlive());
}

TEST_CASE_METHOD(Fixture, "A shader that failed to load works once it is fixed", "[asset editing]")
{
    const fs::path shaderFile = Temp.Write("mine.hlsl", "this does not compile");
    Temp.Write("helper.hlsli", "#define HELPER_VALUE 1.0\n");
    Temp.Write("mine.mat", R"({ "shader": "mine.hlsl" })");

    AssetManager assets(Gpu, Temp.Path());
    const MaterialID material = assets.Find<MaterialAsset>("mine.mat");

    REQUIRE_FALSE(assets.Get(material));

    Temp.Write("mine.hlsl", std::string(kShaderHeader) + std::string(kShaderFooter));
    Touch(shaderFile);

    CHECK(Contains(assets.ReloadChanged(), assets.Find<ShaderAsset>("mine.hlsl")));
    CHECK(assets.Get(material));
}

namespace
{
    // Three slots: one with textures as Sponza writes them, one with values only, and a see-through one with a texture
    // that isn't there
    constexpr std::string_view kSlotsObj = "mtllib model.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl textured\nf 1 2 3\nusemtl plain\nf 1 2 3\nusemtl glass\nf 1 2 3\n";
    constexpr std::string_view kSlotsMtl = R"(newmtl textured
Ns 7.843137
Kd 0.470400 0.470400 0.470400
d 0.000000
map_Kd textures\color.bmp
map_Disp textures/color_ddn.bmp
map_Ka textures/color.bmp

newmtl plain
Ns 100
Kd 1 0 0

newmtl glass
d 0.5
map_Kd textures/gone.bmp
)";

    struct SlotFixture : Fixture
    {
        SlotFixture()
        {
            Temp.Write("model/model.obj", kSlotsObj);
            Temp.Write("model/model.mtl", kSlotsMtl);
            Temp.Write("model/textures/color.bmp", MakeImage());
            Temp.Write("model/textures/color_ddn.bmp", MakeImage());
        }

        std::string ReadMaterial(std::string_view slot) const
        {
            return ReadFile(Temp.Path() / "model/materials" / (std::string(slot) + ".mat"));
        }
    };
}

TEST_CASE_METHOD(SlotFixture, "An .obj's .mtl gives its slots materials", "[asset editing]")
{
    AssetManager assets(Gpu, Temp.Path());
    const MeshID mesh = assets.Find<MeshAsset>("model/model.obj");

    const SlotMaterials result = slot_materials::Create(assets, mesh);
    CHECK(result.Created.size() == 3);
    CHECK(result.Kept == 0);

    const AssetID color = assets.FindId("model/textures/color.bmp");
    const AssetID normal = assets.FindId("model/textures/color_ddn.bmp");

    SECTION("Textures, without the gray an exporter puts next to them")
    {
        const MaterialData* textured = assets.GetData(slot_materials::Find(assets, mesh, "textured"));
        REQUIRE(textured);
        CHECK(textured->Shader == assets.FindId("engine/shaders/S_Lit.hlsl"));
        CHECK(textured->Settings.RenderMode == MaterialRenderMode::Opaque);
        CHECK(textured->Textures.at("ColorMap") == color);
        CHECK(textured->Textures.at("NormalMap") == normal);
        CHECK_FALSE(textured->Properties.contains("Color"));
        CHECK(textured->Properties.at("Roughness").at(0) == Catch::Approx(1.0f - std::sqrt(7.843137f / 1000.0f)));

        // A normal map holds directions, a color map colors
        CHECK_FALSE(assets.GetInfo(normal)->Settings.value("srgb", true));
        CHECK(assets.GetInfo(color)->Settings.value("srgb", true));
    }

    SECTION("Values only")
    {
        const MaterialData* plain = assets.GetData(slot_materials::Find(assets, mesh, "plain"));
        REQUIRE(plain);
        CHECK(plain->Textures.empty());
        CHECK(plain->Properties.at("Color") == std::vector{ 1.0f, 0.0f, 0.0f, 1.0f });
        CHECK(plain->Properties.at("Roughness").at(0) == Catch::Approx(1.0f - std::sqrt(0.1f)));

        // Left out when they are the shader's own defaults
        CHECK_FALSE(plain->Properties.contains("Metallic"));
        CHECK_FALSE(plain->Properties.contains("EmissiveColor"));
    }

    SECTION("See-through, and a texture that isn't there is only left out")
    {
        const MaterialData* glass = assets.GetData(slot_materials::Find(assets, mesh, "glass"));
        REQUIRE(glass);
        CHECK(glass->Settings.RenderMode == MaterialRenderMode::Transparent);
        CHECK(glass->Properties.at("Color").at(3) == 0.5f);
        CHECK(glass->Textures.empty());
    }
}

TEST_CASE_METHOD(SlotFixture, "Making slot materials again changes nothing", "[asset editing]")
{
    AssetManager assets(Gpu, Temp.Path());
    const MeshID mesh = assets.Find<MeshAsset>("model/model.obj");

    REQUIRE(slot_materials::Create(assets, mesh).Created.size() == 3);
    const std::string textured = ReadMaterial("textured");
    const std::string normalMeta = ReadFile(meta::PathFor(Temp.Path() / "model/textures/color_ddn.bmp"));

    const SlotMaterials again = slot_materials::Create(assets, mesh);
    CHECK(again.Created.empty());
    CHECK(again.Kept == 3);
    CHECK(ReadMaterial("textured") == textured);
    CHECK(ReadFile(meta::PathFor(Temp.Path() / "model/textures/color_ddn.bmp")) == normalMeta);
}

TEST_CASE_METHOD(SlotFixture, "A slot material that is there already is never replaced", "[asset editing]")
{
    Temp.Write("model/materials/plain.mat", kMainMaterial);
    AssetManager assets(Gpu, Temp.Path());

    const SlotMaterials result = slot_materials::Create(assets, assets.Find<MeshAsset>("model/model.obj"));
    CHECK(result.Created.size() == 2);
    CHECK(result.Kept == 1);
    CHECK(ReadMaterial("plain") == kMainMaterial);
}

TEST_CASE_METHOD(Fixture, "Only a project's .obj can get slot materials", "[asset editing]")
{
    AssetManager assets(Gpu, Temp.Path());

    CHECK(slot_materials::Create(assets, assets.Find<MeshAsset>("engine/meshes/SM_Cube.obj")).Created.empty());
    CHECK(slot_materials::Create(assets, {}).Created.empty());
}

TEST_CASE_METHOD(Fixture, "A missing reference keeps the type it was read as", "[asset editing]")
{
    const fs::path textureFile = Temp.Write("image.bmp", MakeImage());
    Temp.Write("mine.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl", "textures": { "ColorMap": "image.bmp", "NormalMap": "no/such.bmp" } })");
    AssetManager assets(Gpu, Temp.Path());

    const MaterialID material = assets.Find<MaterialAsset>("mine.mat");
    const TextureID gone = assets.GetData(material)->Textures.at("NormalMap");

    CHECK(assets.IsMissing(gone));
    CHECK(assets.As<TextureAsset>(gone) == gone);
    CHECK_FALSE(assets.As<MeshAsset>(gone));

    SECTION("And so does one whose file went away")
    {
        const TextureID texture = assets.Find<TextureAsset>("image.bmp");
        REQUIRE(assets.Get(material));

        fs::remove(textureFile);
        assets.Refresh();

        CHECK(assets.IsMissing(texture));
        CHECK(assets.As<TextureAsset>(texture) == texture);
        CHECK(assets.GetData(material)->Textures.at("ColorMap") == texture);
    }
}

TEST_CASE_METHOD(Fixture, "A reference to an asset of the wrong type is dropped", "[asset editing]")
{
    Temp.Write("other.mat", kMainMaterial);
    Temp.Write("mine.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl", "textures": { "ColorMap": "other.mat" } })");
    AssetManager assets(Gpu, Temp.Path());

    const MaterialData* data = assets.GetData(assets.Find<MaterialAsset>("mine.mat"));
    REQUIRE(data);
    CHECK(data->Textures.empty());
}

static float FloatProperty(const FakeGpuResourceManager& gpu, MaterialHandle material, const std::string& name)
{
    const std::vector<std::byte>& bytes = gpu.Materials.at(material.GetRaw()).Properties.at(name);
    REQUIRE(bytes.size() == sizeof(float));

    float value = 0.0f;
    std::memcpy(&value, bytes.data(), sizeof(float));
    return value;
}

TEST_CASE_METHOD(Fixture, "A cutout material cuts by its AlphaCutoff, and only a cutout one", "[asset editing]")
{
    Temp.Write("leaves.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl", "renderMode": "cutout", "properties": { "AlphaCutoff": 0.3 } })");

    {
        AssetManager assets(Gpu, Temp.Path());
        const MaterialID id = assets.Find<MaterialAsset>("leaves.mat");

        const MaterialHandle material = *assets.Get(id);
        REQUIRE(material);
        CHECK(assets.GetData(id)->Settings.RenderMode == MaterialRenderMode::Cutout);
        CHECK(FloatProperty(Gpu, material, "AlphaCutoff") == 0.3f);

        SECTION("It is written back as a cutout")
        {
            CHECK(material_data::Write(*assets.GetData(id), assets).find(R"("renderMode": "cutout")") != std::string::npos);
        }

        SECTION("As an opaque one it doesn't cut, and as a cutout again it does")
        {
            MaterialData data = *assets.GetData(id);
            data.Settings.RenderMode = MaterialRenderMode::Opaque;

            CHECK(assets.Apply(id, data) == AssetUpdate::Updated);
            CHECK(FloatProperty(Gpu, material, "AlphaCutoff") == 0.0f);

            data.Settings.RenderMode = MaterialRenderMode::Cutout;
            CHECK(assets.Apply(id, data) == AssetUpdate::Updated);
            CHECK(FloatProperty(Gpu, material, "AlphaCutoff") == 0.3f);
        }

        SECTION("A dragged slider reaches the shader")
        {
            MaterialData data = *assets.GetData(id);
            data.Properties["AlphaCutoff"] = { 0.7f };

            CHECK(assets.Apply(id, data) == AssetUpdate::Updated);
            CHECK(FloatProperty(Gpu, material, "AlphaCutoff") == 0.7f);
        }
    }

    CHECK(Gpu.NothingAlive());
}

TEST_CASE_METHOD(Fixture, "An opaque material on S_Lit cuts nothing, whatever its AlphaCutoff", "[asset editing]")
{
    Temp.Write("solid.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl", "properties": { "AlphaCutoff": 0.9 } })");
    AssetManager assets(Gpu, Temp.Path());

    const MaterialID id = assets.Find<MaterialAsset>("solid.mat");
    const MaterialHandle material = *assets.Get(id);
    REQUIRE(material);

    CHECK(FloatProperty(Gpu, material, "AlphaCutoff") == 0.0f);
}

TEST_CASE_METHOD(SlotFixture, "An .mtl alpha mask makes a cutout material", "[asset editing]")
{
    Temp.Write("model/model.mtl", std::string(kSlotsMtl) + "\nnewmtl leaves\nmap_Kd textures/color.bmp\nmap_d textures/color_mask.bmp\n");
    Temp.Write("model/model.obj", std::string(kSlotsObj) + "usemtl leaves\nf 1 2 3\n");
    AssetManager assets(Gpu, Temp.Path());

    const MeshID mesh = assets.Find<MeshAsset>("model/model.obj");
    REQUIRE(slot_materials::Create(assets, mesh).Created.size() == 4);

    CHECK(assets.GetData(slot_materials::Find(assets, mesh, "leaves"))->Settings.RenderMode == MaterialRenderMode::Cutout);
    CHECK(assets.GetData(slot_materials::Find(assets, mesh, "plain"))->Settings.RenderMode == MaterialRenderMode::Opaque);
}
