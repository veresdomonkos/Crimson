#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/asset_manager/import_cache.hpp>
#include <crimson/asset_manager/meta_file.hpp>
#include <crimson/asset_manager/slot_materials.hpp>
#include <crimson/asset_manager/importers/texture_importer.hpp>
#include <crimson/asset_manager/importers/material_importer.hpp>
#include <crimson/asset_manager/importers/material_asset.hpp>
#include <crimson/asset_manager/importers/mesh_importer.hpp>
#include <crimson/asset_manager/importers/mesh_asset.hpp>
#include <crimson/asset_manager/importers/scene_asset.hpp>
#include <crimson/asset_manager/importers/scene_importer.hpp>
#include <crimson/asset_manager/importers/shader_importer.hpp>
#include <crimson/asset_manager/importers/shader_asset.hpp>
#include <crimson/asset_manager/importers/texture_asset.hpp>

#include <crimson/core/engine_resources.hpp>
#include <crimson/core/log.hpp>
#include <crimson/scene/scene_renderer.hpp>

#include "asset_manager/engine_renames.hpp"

#include "support/fake_gpu.hpp"
#include "support/scene_fields.hpp"
#include "support/log_counter.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using crimson::tests::FakeGpuResourceManager;
using crimson::tests::FieldOf;
using crimson::tests::LogCounter;
using crimson::tests::TempFolder;

namespace fs = std::filesystem;

constexpr std::string_view kTriangleObj = "v 0 0 0\nv 1 0 0\nv 0 2 0\nf 1 2 3\n";
constexpr std::string_view kValidMaterial = R"({ "shader": "engine/shaders/S_Lit.hlsl" })";
constexpr std::string_view kBrokenShader = "this does not compile";

// A 1x1 uncompressed BMP, as PNGs are hard to write out by hand
static std::string MakeImage()
{
    constexpr std::array<unsigned char, 58> kBytes = {
        'B', 'M', 58, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0,
        40, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 24, 0, 0, 0, 0, 0, 4, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        255, 0, 0, 0
    };
    return std::string(reinterpret_cast<const char*>(kBytes.data()), kBytes.size());
}

TEST_CASE("Scanning finds a folder's assets and skips the rest", "[assets]")
{
    const TempFolder temp;
    temp.Write("meshes/nested/tri.obj", kTriangleObj);
    temp.Write("textures/a.BMP", MakeImage());
    temp.Write("notes.txt", "not an asset");
    temp.Write("shaders/common.hlsli", "// only an include");

    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());

    const MeshID mesh = assets.Find<MeshAsset>("meshes/nested/tri.obj");
    REQUIRE(mesh);
    REQUIRE(assets.GetInfo(mesh));
    CHECK(assets.GetInfo(mesh)->Type.Is<MeshAsset>());
    CHECK(assets.GetInfo(mesh)->Path == "meshes/nested/tri.obj");

    // The extension's case doesn't matter
    CHECK(assets.Find<TextureAsset>("textures/a.BMP"));

    CHECK_FALSE(assets.FindId("notes.txt"));
    CHECK_FALSE(assets.FindId("shaders/common.hlsli"));
    CHECK_FALSE(assets.Find<MeshAsset>("no/such/asset.obj"));
    CHECK(assets.GetInfo(AssetID{}) == nullptr);
}

TEST_CASE("A folder that doesn't exist gives just the engine's assets", "[assets]")
{
    const TempFolder temp;
    FakeGpuResourceManager gpu;

    AssetManager assets(gpu, temp.Path() / "nothing");

    CHECK(assets.Find<MaterialAsset>("engine/materials/M_Default.mat"));
    CHECK(std::ranges::all_of(assets.List(AssetType::Of<MeshAsset>()), [](const AssetInfo* info) { return info->ReadOnly && info->Path.starts_with("engine/"); }));
}

TEST_CASE("The engine's built-in meshes load, at the size they are meant to be", "[assets]")
{
    const TempFolder temp;
    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());

    struct Expected
    {
        const char* Path;
        glm::vec3 Min;
        glm::vec3 Max;
    };

    const std::array kMeshes = {
        Expected{ "engine/meshes/SM_Cube.obj",     { -0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f } },
        Expected{ "engine/meshes/SM_Sphere.obj",   { -0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f } },
        Expected{ "engine/meshes/SM_Cylinder.obj", { -0.5f, -1.0f, -0.5f }, { 0.5f, 1.0f, 0.5f } },
        Expected{ "engine/meshes/SM_Capsule.obj",  { -0.5f, -1.0f, -0.5f }, { 0.5f, 1.0f, 0.5f } },
        Expected{ "engine/meshes/SM_Plane.obj",    { -5.0f,  0.0f, -5.0f }, { 5.0f, 0.0f, 5.0f } },
        Expected{ "engine/meshes/SM_Quad.obj",     { -0.5f, -0.5f,  0.0f }, { 0.5f, 0.5f, 0.0f } },
    };

    const auto near = [](const glm::vec3& a, const glm::vec3& b) { return glm::all(glm::lessThan(glm::abs(a - b), glm::vec3(1e-4f))); };

    for (const Expected& expected : kMeshes)
    {
        CAPTURE(expected.Path);

        const MeshID id = assets.Find<MeshAsset>(expected.Path);
        REQUIRE(id);
        CHECK(assets.GetInfo(id)->ReadOnly);

        const Mesh* mesh = assets.Get(id);
        REQUIRE(mesh);
        CHECK(mesh->SubMeshes.size() == 1);
        CHECK(mesh->MaterialSlots.size() == 1);
        CHECK(near(mesh->Bounds.Min, expected.Min));
        CHECK(near(mesh->Bounds.Max, expected.Max));
    }

    CHECK(assets.List(AssetType::Of<MeshAsset>()).size() == kMeshes.size());
}

TEST_CASE("An asset keeps its id through its meta file", "[assets]")
{
    const TempFolder temp;
    const fs::path file = temp.Write("tri.obj", kTriangleObj);
    FakeGpuResourceManager gpu;

    const AssetID first = AssetManager(gpu, temp.Path()).Find<MeshAsset>("tri.obj");
    REQUIRE(first);

    CHECK(fs::exists(fs::path(file) += ".meta"));

    SECTION("When reopened")
    {
        CHECK(AssetManager(gpu, temp.Path()).Find<MeshAsset>("tri.obj") == first);
    }

    SECTION("When the file is moved along with its meta")
    {
        fs::create_directories(temp.Path() / "moved");
        fs::rename(file, temp.Path() / "moved/tri.obj");
        fs::rename(fs::path(file) += ".meta", temp.Path() / "moved/tri.obj.meta");

        AssetManager assets(gpu, temp.Path());

        CHECK(assets.Find<MeshAsset>("moved/tri.obj") == first);
        CHECK_FALSE(assets.Find<MeshAsset>("tri.obj"));
    }

    SECTION("Unless the meta is lost")
    {
        fs::remove(fs::path(file) += ".meta");
        CHECK(AssetManager(gpu, temp.Path()).Find<MeshAsset>("tri.obj") != first);
    }
}

TEST_CASE("A corrupt meta file gets replaced", "[assets]")
{
    const TempFolder temp;
    temp.Write("tri.obj", kTriangleObj);
    temp.Write("tri.obj.meta", "{ not json");
    FakeGpuResourceManager gpu;

    const AssetID id = AssetManager(gpu, temp.Path()).Find<MeshAsset>("tri.obj");

    CHECK(id);
    CHECK(AssetManager(gpu, temp.Path()).Find<MeshAsset>("tri.obj") == id);
}

TEST_CASE("A file copied along with its meta gets its own id", "[assets]")
{
    const TempFolder temp;
    const fs::path original = temp.Write("a.obj", kTriangleObj);
    FakeGpuResourceManager gpu;

    { AssetManager first(gpu, temp.Path()); }

    fs::copy_file(original, temp.Path() / "b.obj");
    fs::copy_file(fs::path(original) += ".meta", temp.Path() / "b.obj.meta");

    AssetManager assets(gpu, temp.Path());

    const MeshID a = assets.Find<MeshAsset>("a.obj");
    const MeshID b = assets.Find<MeshAsset>("b.obj");

    REQUIRE(a);
    REQUIRE(b);
    CHECK(a != b);
    CHECK(assets.GetInfo(a)->Path == "a.obj");
    CHECK(assets.GetInfo(b)->Path == "b.obj");

    // And it sticks
    AssetManager reopened(gpu, temp.Path());
    CHECK(reopened.Find<MeshAsset>("a.obj") != reopened.Find<MeshAsset>("b.obj"));
}

TEST_CASE("Listing is sorted by path and filtered by type", "[assets]")
{
    const TempFolder temp;
    temp.Write("z.obj", kTriangleObj);
    temp.Write("a.obj", kTriangleObj);
    temp.Write("m/b.obj", kTriangleObj);
    temp.Write("image.bmp", MakeImage());
    FakeGpuResourceManager gpu;

    AssetManager assets(gpu, temp.Path());

    // The engine's own meshes are listed too, but only the project's are under test here
    std::vector<std::string> paths;
    for (const AssetInfo* info : assets.List(AssetType::Of<MeshAsset>()))
    {
        if (!info->ReadOnly)
            paths.push_back(info->Path);
    }

    CHECK(paths == std::vector<std::string>{ "a.obj", "m/b.obj", "z.obj" });
}

TEST_CASE("Engine assets are always there, with the same id everywhere", "[assets]")
{
    const TempFolder projectA;
    const TempFolder projectB;
    FakeGpuResourceManager gpu;

    AssetManager a(gpu, projectA.Path());
    AssetManager b(gpu, projectB.Path());

    for (const char* path : { "engine/materials/M_Default.mat", "engine/materials/internal/M_ToneMap.mat", "engine/materials/internal/M_Outline.mat",
                              "engine/shaders/S_Lit.hlsl" })
    {
        CAPTURE(path);
        CHECK(a.FindId(path));
        CHECK(a.FindId(path) == b.FindId(path));
    }

    CHECK(slot_materials::DefaultMaterial(a) == a.Find<MaterialAsset>("engine/materials/M_Default.mat"));

    // Includes aren't assets, and engine assets don't write metas anywhere
    CHECK_FALSE(a.FindId("engine/shaders/crimson/bindings.hlsli"));
    CHECK(fs::is_empty(projectA.Path()));
}

TEST_CASE("Every engine material loads, without any project files", "[assets]")
{
    const TempFolder temp;
    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());

    const auto materials = assets.List(AssetType::Of<MaterialAsset>());
    REQUIRE_FALSE(materials.empty());

    for (const AssetInfo* info : materials)
    {
        CAPTURE(info->Path);
        CHECK(assets.Get(assets.As<MaterialAsset>(info->Id)));
    }
}

TEST_CASE("A project can't shadow the engine's assets", "[assets]")
{
    const TempFolder temp;
    temp.Write("engine/materials/M_Default.mat", R"({ "shader": "nope" })");
    FakeGpuResourceManager gpu;

    AssetManager assets(gpu, temp.Path());

    CHECK(assets.Get<MaterialAsset>("engine/materials/M_Default.mat"));
    CHECK_FALSE(fs::exists(temp.Path() / "engine/materials/M_Default.mat.meta"));
}

TEST_CASE("A project's material can use an engine shader, or its own", "[assets]")
{
    const TempFolder temp;
    temp.Write("mine.mat", R"({
        "shader": "engine/shaders/S_Lit.hlsl",
        "renderMode": "transparent",
        "cull": "none",
        "properties": { "Color": [0.1, 0.2, 0.3, 1.0] },
        "textures": { "Albedo": "image.bmp" }
    })");
    temp.Write("image.bmp", MakeImage());
    temp.Write("deep/down/custom.hlsl", R"(
#include "crimson/lighting.hlsli"
float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }
float4 PSMain() : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }
)");
    temp.Write("custom.mat", R"({ "shader": "deep/down/custom.hlsl" })");

    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());

    SECTION("Engine shader, with settings, properties and textures")
    {
        const MaterialHandle material = *assets.Get<MaterialAsset>("mine.mat");
        REQUIRE(material);

        const auto& record = gpu.Materials.at(material.GetRaw());
        CHECK(record.Settings.RenderMode == MaterialRenderMode::Transparent);
        CHECK(record.Settings.Cull == CullMode::None);
        CHECK(record.Properties.at("Color").size() == sizeof(glm::vec4));
        CHECK(record.Textures.at("Albedo") == *assets.Get<TextureAsset>("image.bmp"));
    }

    SECTION("Own shader, in a subfolder, using the engine's includes")
    {
        CHECK(assets.Get<MaterialAsset>("custom.mat"));
    }
}

TEST_CASE("A load only happens once", "[assets]")
{
    const TempFolder temp;
    temp.Write("a.mat", kValidMaterial);
    temp.Write("b.mat", kValidMaterial);
    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());

    CHECK(gpu.LiveMaterials == 0);

    const MaterialHandle first = *assets.Get<MaterialAsset>("a.mat");
    CHECK(*assets.Get<MaterialAsset>("a.mat") == first);
    CHECK(assets.Get(assets.Find<MaterialAsset>("a.mat")) == assets.Get(assets.Find<MaterialAsset>("a.mat")));
    CHECK(assets.Get<MaterialAsset>("a.mat") == assets.Get(assets.Find<MaterialAsset>("a.mat")));
    CHECK(*assets.Get(assets.Find<MaterialAsset>("a.mat")) == first);
    CHECK(gpu.LiveMaterials == 1);

    // Two materials share one shader
    CHECK(assets.Get<MaterialAsset>("b.mat"));
    CHECK(gpu.LiveMaterials == 2);
    CHECK(gpu.ShadersCreated == 1);
}

TEST_CASE("A failed load is logged once and not tried again", "[assets]")
{
    const TempFolder temp;
    temp.Write("broken.hlsl", kBrokenShader);
    temp.Write("uses_broken.mat", R"({ "shader": "broken.hlsl" })");
    temp.Write("also_uses_broken.mat", R"({ "shader": "broken.hlsl" })");

    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());
    const LogCounter errors(LogLevel::Error, "Compilation failed");

    for (int i = 0; i < 3; ++i)
    {
        CHECK_FALSE(assets.Get<ShaderAsset>("broken.hlsl"));
        CHECK_FALSE(assets.Get<MaterialAsset>("uses_broken.mat"));
    }

    CHECK_FALSE(assets.Get<MaterialAsset>("also_uses_broken.mat"));

    CHECK(errors.Count() == 1);
    CHECK(gpu.ShadersCreated == 0);
    CHECK(gpu.LiveMaterials == 0);
}

TEST_CASE("Materials that can't load fail cleanly", "[assets]")
{
    const TempFolder temp;
    FakeGpuResourceManager gpu;

    SECTION("Broken JSON")
    {
        temp.Write("m.mat", "{ not json");
        AssetManager assets(gpu, temp.Path());
        CHECK_FALSE(assets.Get<MaterialAsset>("m.mat"));
    }

    SECTION("No shader")
    {
        temp.Write("m.mat", "{}");
        AssetManager assets(gpu, temp.Path());
        CHECK_FALSE(assets.Get<MaterialAsset>("m.mat"));
    }

    SECTION("A shader that doesn't exist")
    {
        temp.Write("m.mat", R"({ "shader": "nope.hlsl" })");
        AssetManager assets(gpu, temp.Path());
        CHECK_FALSE(assets.Get<MaterialAsset>("m.mat"));
    }

    SECTION("A missing texture only loses the texture")
    {
        temp.Write("m.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl", "textures": { "Albedo": "nope.bmp" } })");
        AssetManager assets(gpu, temp.Path());
        const MaterialHandle material = *assets.Get<MaterialAsset>("m.mat");
        REQUIRE(material);
        CHECK_FALSE(gpu.Materials.at(material.GetRaw()).Textures.contains("Albedo"));
    }
}

TEST_CASE("Empty and mistyped requests get nothing", "[assets]")
{
    const TempFolder temp;
    temp.Write("tri.obj", kTriangleObj);
    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());

    CHECK_FALSE(assets.Get(MaterialID{}));
    CHECK(assets.Get(MeshID{}) == nullptr);
    CHECK_FALSE(assets.Get<MaterialAsset>("no/such.mat"));

    SECTION("Asking for the wrong type doesn't load it as that")
    {
        CHECK_FALSE(assets.Get<MaterialAsset>("tri.obj"));
        CHECK_FALSE(assets.Get<TextureAsset>("tri.obj"));
        CHECK_FALSE(assets.Get<ShaderAsset>("tri.obj"));
        CHECK(assets.Get<MeshAsset>("tri.obj") != nullptr);
    }
}

TEST_CASE("Typed ids only come from assets of their type", "[assets]")
{
    const TempFolder temp;
    temp.Write("tri.obj", kTriangleObj);
    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());

    const MeshID untyped = assets.Find<MeshAsset>("tri.obj");
    const MeshID mesh = assets.Find<MeshAsset>("tri.obj");

    CHECK(mesh);
    CHECK(mesh == assets.As<MeshAsset>(untyped));
    CHECK(static_cast<AssetID>(mesh) == untyped);
    CHECK(assets.Get(mesh) != nullptr);
    CHECK(assets.Get<MeshAsset>("tri.obj") == assets.Get(mesh));

    SECTION("Another type gives an empty id")
    {
        CHECK_FALSE(assets.As<TextureAsset>(untyped));
        CHECK_FALSE(assets.Find<MaterialAsset>("tri.obj"));
        CHECK_FALSE(assets.Get<MaterialAsset>("tri.obj"));
    }

    SECTION("So does an id or path that doesn't exist")
    {
        CHECK_FALSE(assets.As<MeshAsset>(AssetID::Generate()));
        CHECK_FALSE(assets.Find<MeshAsset>("no/such.obj"));
        CHECK(assets.Get(MeshID{}) == nullptr);
    }

    SECTION("Engine assets have types too")
    {
        const ShaderID shader = assets.Find<ShaderAsset>("engine/shaders/S_Lit.hlsl");
        REQUIRE(shader);
        CHECK(assets.Get(shader));
    }
}

TEST_CASE("Meshes load their geometry, bounds and material slots", "[assets]")
{
    const TempFolder temp;
    FakeGpuResourceManager gpu;

    SECTION("Without materials, they get one slot")
    {
        temp.Write("tri.obj", kTriangleObj);
        AssetManager assets(gpu, temp.Path());

        const Mesh* mesh = assets.Get<MeshAsset>("tri.obj");
        REQUIRE(mesh);
        CHECK(mesh->VertexBuffer);
        CHECK(mesh->IndexBuffer);
        CHECK(mesh->MaterialSlots == std::vector<std::string>{ "default" });
        REQUIRE(mesh->SubMeshes.size() == 1);
        CHECK(mesh->SubMeshes[0].IndexCount == 3);
        CHECK(mesh->Bounds.Min == glm::vec3(0.0f));
        CHECK(mesh->Bounds.Max == glm::vec3(1.0f, 2.0f, 0.0f));
    }

    SECTION("With a material file next to it")
    {
        temp.Write("tri.mtl", "newmtl red\nKd 1 0 0\nnewmtl blue\nKd 0 0 1\n");
        temp.Write("tri.obj", "mtllib tri.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl blue\nf 1 2 3\n");
        AssetManager assets(gpu, temp.Path());

        const Mesh* mesh = assets.Get<MeshAsset>("tri.obj");
        REQUIRE(mesh);
        CHECK(mesh->MaterialSlots.size() == 2);
        REQUIRE(mesh->SubMeshes.size() == 1);
        CHECK(mesh->MaterialSlots[mesh->SubMeshes[0].MaterialSlot] == "blue");
    }

    SECTION("A file without geometry fails")
    {
        temp.Write("empty.obj", "# nothing here\n");
        AssetManager assets(gpu, temp.Path());
        CHECK(assets.Get<MeshAsset>("empty.obj") == nullptr);
    }
}

TEST_CASE("Assets load from folders with non-ASCII names", "[assets]")
{
    const TempFolder temp;
    const fs::path folder = fs::path(u8"Játék");

    temp.Write(folder / "tri.mtl", "newmtl red\nKd 1 0 0\n");
    temp.Write(folder / "tri.obj", "mtllib tri.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nusemtl red\nf 1 2 3\n");
    temp.Write(folder / "image.bmp", MakeImage());
    temp.Write(folder / "own.hlsli", "#define VALUE 1.0\n");
    temp.Write(folder / "shader.hlsl", R"(
#include "own.hlsli"
float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, VALUE); }
float4 PSMain() : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }
)");
    temp.Write(folder / "own.mat", R"({ "shader": "shader.hlsl" })");

    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path() / folder);

    const Mesh* mesh = assets.Get<MeshAsset>("tri.obj");
    REQUIRE(mesh);
    CHECK(mesh->MaterialSlots == std::vector<std::string>{ "red" });
    CHECK(assets.Get<TextureAsset>("image.bmp"));
    CHECK(assets.Get<MaterialAsset>("own.mat"));
}

TEST_CASE("Images decode from memory", "[assets]")
{
    FakeGpuResourceManager gpu;

    SECTION("A valid one")
    {
        const std::string image = MakeImage();
        CHECK(CreateTextureFromMemory(gpu, std::as_bytes(std::span(image))));
        CHECK(gpu.LastTextureInfo.Width == 1);
        CHECK(gpu.LastTextureInfo.Height == 1);
    }

    SECTION("Garbage")
    {
        const std::string garbage = "not an image at all";
        CHECK_FALSE(CreateTextureFromMemory(gpu, std::as_bytes(std::span(garbage))));
        CHECK(gpu.LiveTextures == 0);
    }
}

TEST_CASE("A texture's srgb setting picks its format", "[assets]")
{
    const TempFolder temp;
    temp.Write("image.bmp", MakeImage());

    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());
    const TextureID id = assets.Find<TextureAsset>("image.bmp");

    REQUIRE(assets.Get(id));
    CHECK(gpu.LastTextureInfo.Format == TextureFormat::RGBA8Srgb);

    SECTION("Turning it off")
    {
        REQUIRE(assets.Reimport(id, { { "srgb", false } }));
        CHECK(gpu.LastTextureInfo.Format == TextureFormat::RGBA8);
    }
}

TEST_CASE("Destroying the manager frees what it loaded, users first", "[assets]")
{
    const TempFolder temp;
    temp.Write("tri.obj", kTriangleObj);
    temp.Write("image.bmp", MakeImage());
    temp.Write("mine.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl", "textures": { "Albedo": "image.bmp" } })");

    FakeGpuResourceManager gpu;

    {
        AssetManager assets(gpu, temp.Path());
        CHECK(assets.Get<MeshAsset>("tri.obj"));
        CHECK(assets.Get<MaterialAsset>("mine.mat"));

        CHECK(gpu.LiveMaterials == 1);
        CHECK(gpu.LiveShaders == 1);
        CHECK(gpu.LiveTextures == 2); // with the flat normal the shader's empty slot uses
    }

    CHECK(gpu.NothingAlive());

    // A material can't outlive the shader it was made from
    const auto firstShader = std::ranges::find(gpu.DestroyOrder, "shader");
    const auto lastMaterial = std::ranges::find_last(gpu.DestroyOrder, "material").begin();
    CHECK(lastMaterial < firstShader);
}

TEST_CASE("A mesh's empty slots find the materials made for them", "[assets]")
{
    const TempFolder temp;
    temp.Write("model/two.obj", "mtllib two.mtl\nv 0 0 0\nv 1 0 0\nv 0 2 0\nv 0 0 1\nusemtl stone\nf 1 2 3\nusemtl wood\nf 1 2 4\n");
    temp.Write("model/two.mtl", "newmtl stone\nnewmtl wood\n");
    temp.Write("model/materials/stone.mat", kValidMaterial);
    temp.Write("materials/wood.mat", kValidMaterial); // not next to this mesh
    temp.Write("mine.mat", kValidMaterial);

    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());

    const MeshID mesh = assets.Find<MeshAsset>("model/two.obj");
    const MaterialID stone = assets.Find<MaterialAsset>("model/materials/stone.mat");

    CHECK(slot_materials::Find(assets, mesh, "stone") == stone);
    CHECK_FALSE(slot_materials::Find(assets, mesh, "wood"));
    CHECK_FALSE(slot_materials::Find(assets, MeshID{}, "stone"));

    // What a renderer's empty slot draws with
    const MaterialID defaultMaterial = slot_materials::DefaultMaterial(assets);
    REQUIRE(defaultMaterial);
    CHECK(slot_materials::ForEmptySlot(assets, mesh, "stone") == stone);
    CHECK(slot_materials::ForEmptySlot(assets, mesh, "wood") == defaultMaterial);

    // What a missing material draws as is its own, in the engine's internal materials
    CHECK(slot_materials::MissingMaterial(assets));
    CHECK(slot_materials::MissingMaterial(assets) != defaultMaterial);
    CHECK(assets.Get(slot_materials::MissingMaterial(assets)));
}

static std::string ReadFile(const fs::path& file)
{
    std::ifstream stream(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

// The id an engine asset has at a path
static std::string EngineIdOf(std::string_view path)
{
    return AssetID::FromName(kAssetPathNamespace, path).ToString();
}

TEST_CASE("Engine assets saved under their old names still load", "[assets]")
{
    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, fs::path("does/not/exist"));

    const AssetID lit = assets.FindId("engine/shaders/S_Lit.hlsl");
    const AssetID cube = assets.FindId("engine/meshes/SM_Cube.obj");
    REQUIRE(lit);
    REQUIRE(cube);

    SECTION("A material, by id and path or by path alone")
    {
        const std::string saved = R"({ "shader": { "guid": ")" + EngineIdOf("engine/shaders/main.hlsl") + R"(", "path": "engine/shaders/main.hlsl" } })";

        for (const std::string& text : { saved, std::string(R"({ "shader": "engine/shaders/main.hlsl" })") })
        {
            const auto data = material_data::Parse(text, assets, "test");
            REQUIRE(data);
            CHECK(data->Shader == lit);
            CHECK_THAT(material_data::Write(*data, assets), Catch::Matchers::ContainsSubstring(R"("path": "engine/shaders/S_Lit.hlsl")"));
        }
    }

    SECTION("A scene")
    {
        const std::string text = R"({ "version": 1, "entities": [ { "id": "00000000-0000-4000-8000-0000000000b2", "meshRenderer": { "mesh": { "guid": ")" + EngineIdOf("engine/meshes/cube.obj")
            + R"(", "path": "engine/meshes/cube.obj" }, "materials": [ "engine/materials/default.mat" ] } } ] })";

        const auto data = scene_data::Parse(text, assets, "test");
        REQUIRE(data);
        CHECK(FieldOf<AssetID>(assets, data->Roots.at(0), "meshRenderer", "mesh") == cube);
        CHECK(FieldOf<std::vector<AssetID>>(assets, data->Roots.at(0), "meshRenderer", "materials").at(0) == slot_materials::DefaultMaterial(assets));

        const std::string written = scene_data::Write(*data, assets);
        CHECK_THAT(written, Catch::Matchers::ContainsSubstring("engine/meshes/SM_Cube.obj"));
        CHECK_THAT(written, Catch::Matchers::ContainsSubstring("engine/materials/M_Default.mat"));
    }

    SECTION("An id that was never an engine asset's stays as it is")
    {
        const AssetID other = AssetID::Generate();
        CHECK(assets.Redirect(other) == other);
        CHECK(assets.Redirect(lit) == lit);
    }
}

TEST_CASE("The renderer's own shaders and materials are internal", "[assets]")
{
    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, fs::path("does/not/exist"));

    for (const std::string_view path : { "engine/shaders/internal/S_ToneMap.hlsl", "engine/materials/internal/M_Outline.mat", "engine/materials/internal/M_Missing.mat", "engine/scenes/internal/SC_NewScene.scene" })
    {
        const AssetInfo* info = assets.GetInfo(assets.FindId(path));
        REQUIRE(info);
        CHECK(IsInternal(*info));
    }

    for (const std::string_view path : { "engine/shaders/S_Lit.hlsl", "engine/materials/M_Default.mat", "engine/meshes/SM_Cube.obj" })
    {
        const AssetInfo* info = assets.GetInfo(assets.FindId(path));
        REQUIRE(info);
        CHECK_FALSE(IsInternal(*info));
    }
}

TEST_CASE("An engine asset renamed twice is found from either old name", "[assets]")
{
    const auto exists = [](std::string_view path) { return path == "c"; };

    constexpr std::array<EngineRename, 2> twice = { { { "a", "b" }, { "b", "c" } } };
    CHECK(FollowRenames(twice, "a", exists) == "c");
    CHECK(FollowRenames(twice, "b", exists) == "c");
    CHECK(FollowRenames(twice, "c", exists) == "c");
    CHECK(FollowRenames(twice, "d", exists).empty());

    constexpr std::array<EngineRename, 2> loop = { { { "x", "y" }, { "y", "x" } } };
    CHECK(FollowRenames(loop, "x", exists).empty());
}

TEST_CASE("Opening a project writes the engine's new names into its files", "[assets]")
{
    const TempFolder temp;
    const fs::path material = temp.Write("mine.mat", R"({ "shader": "engine/shaders/main.hlsl" })");
    const fs::path scene = temp.Write("level.scene", R"({ "version": 1, "entities": [ { "meshRenderer": { "mesh": { "guid": ")"
        + EngineIdOf("engine/meshes/cube.obj") + R"(", "path": "engine/meshes/cube.obj" } } } ] })");

    FakeGpuResourceManager gpu;
    const AssetManager assets(gpu, temp.Path());

    const std::string materialText = ReadFile(material);
    CHECK_THAT(materialText, Catch::Matchers::ContainsSubstring("engine/shaders/S_Lit.hlsl"));
    CHECK_THAT(materialText, !Catch::Matchers::ContainsSubstring("main.hlsl"));

    const std::string sceneText = ReadFile(scene);
    CHECK_THAT(sceneText, Catch::Matchers::ContainsSubstring(R"("path": "engine/meshes/SM_Cube.obj")"));
    CHECK_THAT(sceneText, Catch::Matchers::ContainsSubstring(assets.FindId("engine/meshes/SM_Cube.obj").ToString()));
    CHECK_THAT(sceneText, !Catch::Matchers::ContainsSubstring("meshes/cube.obj"));
}

TEST_CASE("A scene file's entities get ids that stay", "[assets]")
{
    constexpr std::string_view kIdless = R"({ "version": 1, "entities": [ { "name": "A", "children": [ { "name": "B" } ] }, { "name": "C" } ] })";

    const TempFolder temp;
    const fs::path file = temp.Write("level.scene", kIdless);

    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());
    const SceneID id = assets.Find<SceneAsset>("level.scene");
    REQUIRE(id);

    const auto first = assets.Read(id);
    const auto second = assets.Read(id);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(*first == *second);
    CHECK(first->Roots[0].Id);
    CHECK_THAT(ReadFile(file), Catch::Matchers::ContainsSubstring(first->Roots[0].Id.ToString()));

    SECTION("Another manager over the folder reads the same ids")
    {
        const AssetManager other(gpu, temp.Path());
        const auto again = other.Read(other.Find<SceneAsset>("level.scene"));
        REQUIRE(again);
        CHECK(*again == *first);
    }

    SECTION("A file edited back to no ids after the manager was built gets them again, and keeps them")
    {
        temp.Write("level.scene", kIdless);

        const auto third = assets.Read(id);
        const auto fourth = assets.Read(id);
        REQUIRE(third);
        REQUIRE(fourth);
        CHECK(*third == *fourth);
    }
}

TEST_CASE("A scene file that can't be given ids doesn't load", "[assets]")
{
    const TempFolder temp;
    temp.Write("level.scene", R"({ "version": 1, "entities": [ { "name": "A" } ] })");

    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());
    const SceneID id = assets.Find<SceneAsset>("level.scene");
    REQUIRE(id);

    // A directory where the temporary file goes, for the write to fail whatever the platform says about permissions
    fs::create_directory(temp.Path() / "level.scene.saving");

    const LogCounter errors(LogLevel::Error, "without an id");
    CHECK_FALSE(assets.Read(id));
    CHECK(errors.Count() == 1);
}

TEST_CASE("Every engine scene loads", "[assets]")
{
    FakeGpuResourceManager gpu;
    const AssetManager assets(gpu, fs::path("does/not/exist"));

    int scenes = 0;

    for (const EmbeddedFile& file : GetEngineResources())
    {
        if (!file.Path.ends_with(".scene"))
            continue;

        ++scenes;
        DYNAMIC_SECTION(file.Path)
        {
            CHECK(assets.Read(assets.Find<SceneAsset>("engine/" + std::string(file.Path))));
        }
    }

    CHECK(scenes > 0);
}

TEST_CASE("New scenes share no ids with each other or the template", "[assets]")
{
    const TempFolder temp;
    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());

    const auto first = assets.Read(assets.Create<SceneAsset>("a.scene"));
    const auto second = assets.Read(assets.Create<SceneAsset>("b.scene"));
    const auto base = assets.Read(assets.Find<SceneAsset>("engine/scenes/internal/SC_NewScene.scene"));
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(base);

    CHECK(first->Roots[0].Id != second->Roots[0].Id);
    CHECK(first->Roots[0].Id != base->Roots[0].Id);
    CHECK(second->Roots[0].Id != base->Roots[0].Id);
}

namespace
{
    // The engine's cache is shared by the whole process, so a test that gives it a folder has to take it back
    class EngineCacheFolder
    {
    public:
        explicit EngineCacheFolder(const fs::path& folder) { ImportCache::Engine().SetFolder(folder); }
        ~EngineCacheFolder() { ImportCache::Engine().SetFolder({}); }

        EngineCacheFolder(const EngineCacheFolder&) = delete;
        EngineCacheFolder& operator=(const EngineCacheFolder&) = delete;
    };
}

TEST_CASE("The engine's shaders use the engine cache", "[assets]")
{
    const TempFolder temp;
    const fs::path engineCache = temp.Path() / "engine_cache";
    const fs::path projectCache = temp.Path() / ".crimson" / "cache";

    ImportCache::ForgetMemory();
    const EngineCacheFolder guard(engineCache);

    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path() / "assets", projectCache);
    REQUIRE(assets.Get<ShaderAsset>("engine/shaders/S_Lit.hlsl"));

    REQUIRE(fs::is_directory(engineCache / "shaders"));
    CHECK_FALSE(fs::is_empty(engineCache / "shaders"));
    CHECK_FALSE(fs::exists(projectCache));
}

TEST_CASE("A moved project still loads its shaders from its cache", "[assets]")
{
    const TempFolder temp;
    temp.Write("a/assets/helper.hlsli", "#define HELPER_VALUE 1.0\n");
    temp.Write("a/assets/mine.hlsl", R"(
#include "helper.hlsli"
float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, HELPER_VALUE); }
float4 PSMain() : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }
)");

    FakeGpuResourceManager gpu;
    ImportCache::ForgetMemory();

    {
        AssetManager assets(gpu, temp.Path() / "a" / "assets", temp.Path() / "a" / ".crimson" / "cache");
        REQUIRE(assets.Get<ShaderAsset>("mine.hlsl"));
    }

    ImportCache::ForgetMemory();
    fs::rename(temp.Path() / "a", temp.Path() / "b");

    const ImportCacheStats before = ImportCache::Stats();
    AssetManager assets(gpu, temp.Path() / "b" / "assets", temp.Path() / "b" / ".crimson" / "cache");
    CHECK(assets.Get<ShaderAsset>("mine.hlsl"));
    CHECK(ImportCache::Stats().Hits - before.Hits == 1);
    CHECK(ImportCache::Stats().Stores == before.Stores);

    // The restored dependency has to be the moved file, or this change wouldn't reload it
    const fs::path helper = temp.Write("b/assets/helper.hlsli", "#define HELPER_VALUE 2.0\n");
    fs::last_write_time(helper, fs::last_write_time(helper) + std::chrono::seconds(2));

    const ImportCacheStats beforeChange = ImportCache::Stats();
    CHECK_FALSE(assets.ReloadChanged().empty());
    CHECK(ImportCache::Stats().Stores - beforeChange.Stores == 1);
}

namespace
{
    // The file that makes an asset of the type: asset_Texture.png, empty
    template <typename T, typename Importer>
    fs::path TypeFileName()
    {
        const Importer importer;
        return "asset_" + std::string(T::Name) + std::string(importer.Extensions().front());
    }

    template <typename T, typename Importer>
    void CheckRegistered(AssetManager& assets, const TempFolder& temp)
    {
        const std::vector<AssetType> types = assets.Types();
        const auto position = [&types](AssetType type) { return std::ranges::find(types, type) - types.begin(); };
        const std::ptrdiff_t at = position(AssetType::Of<T>());
        CHECK(at < std::ssize(types));

        // Every entry of Uses comes before it, which is the order they are unloaded in, reversed
        for (const AssetType used : T::Uses)
            CHECK(position(used) < at);

        // The importer is registered, as it made this one's info
        const Importer importer;
        CHECK(importer.Type() == AssetType::Of<T>());

        const AssetID id = assets.FindId(TypeFileName<T, Importer>().generic_string());
        REQUIRE(assets.GetInfo(id));
        CHECK(assets.GetInfo(id)->Type.Is<T>());

        // The name goes into files and comes out as itself
        std::ifstream meta(meta::PathFor(temp.Path() / TypeFileName<T, Importer>()));
        CHECK(nlohmann::json::parse(meta)["type"].get<std::string>() == T::Name);

        // Its store loads it or fails to, and says which
        if constexpr (LoadableAsset<T>)
        {
            const auto loaded = assets.Get(assets.As<T>(id));
            CHECK(assets.IsLoaded(id) == static_cast<bool>(loaded));
        }
    }
}

TEST_CASE("The built-in asset types register completely, each after what it uses", "[assets]")
{
    static_assert(CompleteAssetType<TextureAsset> && CompleteAssetType<MeshAsset> && CompleteAssetType<ShaderAsset>
        && CompleteAssetType<MaterialAsset> && CompleteAssetType<SceneAsset>);
    static_assert(ImporterFor<TextureImporter, TextureAsset> && ImporterFor<MeshImporter, MeshAsset> && ImporterFor<ShaderImporter, ShaderAsset>
        && ImporterFor<MaterialImporter, MaterialAsset> && ImporterFor<SceneImporter, SceneAsset>);

    const TempFolder temp;
    temp.Write(TypeFileName<TextureAsset, TextureImporter>(), "");
    temp.Write(TypeFileName<MeshAsset, MeshImporter>(), "");
    temp.Write(TypeFileName<ShaderAsset, ShaderImporter>(), "");
    temp.Write(TypeFileName<MaterialAsset, MaterialImporter>(), "");
    temp.Write(TypeFileName<SceneAsset, SceneImporter>(), "");

    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());

    CheckRegistered<TextureAsset, TextureImporter>(assets, temp);
    CheckRegistered<MeshAsset, MeshImporter>(assets, temp);
    CheckRegistered<ShaderAsset, ShaderImporter>(assets, temp);
    CheckRegistered<MaterialAsset, MaterialImporter>(assets, temp);
    CheckRegistered<SceneAsset, SceneImporter>(assets, temp);
}

namespace
{
    // Two types made only for the test: a whole is made of parts
    struct PartAsset
    {
        using Data = NoAssetData;

        static constexpr std::string_view Name = "Part";
        static constexpr std::array<AssetType, 0> Uses{};
    };

    struct WholeAsset
    {
        using Data = NoAssetData;

        static constexpr std::string_view Name = "Whole";
        static constexpr std::array<AssetType, 1> Uses = { AssetType::Of<PartAsset>() };
    };

    class PartImporter final : public AssetImporterBase
    {
    public:
        AssetType Type() const override { return AssetType::Of<PartAsset>(); }
        nlohmann::json DefaultSettings() const override { return nlohmann::json::object(); }

        std::span<const std::string_view> Extensions() const override
        {
            static constexpr std::string_view exts[] = { ".testpart" };
            return exts;
        }
    };

    class WholeImporter final : public AssetImporterBase
    {
    public:
        AssetType Type() const override { return AssetType::Of<WholeAsset>(); }
        nlohmann::json DefaultSettings() const override { return nlohmann::json::object(); }

        std::span<const std::string_view> Extensions() const override
        {
            static constexpr std::string_view exts[] = { ".testwhole" };
            return exts;
        }
    };

    // Has no name, which would make it the same as no type
    struct NamelessAsset
    {
        using Data = NoAssetData;

        static constexpr std::string_view Name = "";
        static constexpr std::array<AssetType, 0> Uses{};
    };
}

TEST_CASE("An asset type is refused until what it uses is registered", "[assets]")
{
    static_assert(CompleteAssetType<PartAsset> && CompleteAssetType<WholeAsset> && CompleteAssetType<NamelessAsset>);

    const TempFolder temp;
    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());

    {
        const LogCounter errors(LogLevel::Error, "the Part it uses isn't registered yet");
        CHECK_FALSE(assets.RegisterType<WholeAsset, WholeImporter>());
        CHECK(errors.Count() == 1);
    }

    std::vector<AssetType> types = assets.Types();
    CHECK(std::ranges::find(types, AssetType::Of<WholeAsset>()) == types.end());

    CHECK(assets.RegisterType<PartAsset, PartImporter>());
    CHECK(assets.RegisterType<WholeAsset, WholeImporter>());

    types = assets.Types();
    const auto part = std::ranges::find(types, AssetType::Of<PartAsset>());
    const auto whole = std::ranges::find(types, AssetType::Of<WholeAsset>());
    REQUIRE(part != types.end());
    REQUIRE(whole != types.end());
    CHECK(part < whole);

    {
        const LogCounter errors(LogLevel::Error, "is registered already");
        CHECK_FALSE(assets.RegisterType<TextureAsset, TextureImporter>());
        CHECK(errors.Count() == 1);
    }

    {
        const LogCounter errors(LogLevel::Error, "with no name");
        CHECK_FALSE(assets.RegisterType<NamelessAsset, PartImporter>());
        CHECK(errors.Count() == 1);
    }
}

TEST_CASE("Asset types keep the names their files have", "[assets]")
{
    CHECK(TextureAsset::Name == "Texture");
    CHECK(MeshAsset::Name == "Mesh");
    CHECK(ShaderAsset::Name == "Shader");
    CHECK(MaterialAsset::Name == "Material");
    CHECK(SceneAsset::Name == "Scene");

    CHECK(AssetType::Of<MaterialAsset>().Name() == "Material");
    CHECK(AssetType::Of<MaterialAsset>() != AssetType::Of<SceneAsset>());

    CHECK_FALSE(AssetType{});
    CHECK(AssetType{}.Name().empty());
}

TEST_CASE("A meta file naming another type is warned about, and its importer's type wins", "[assets]")
{
    const TempFolder temp;
    temp.Write("tri.obj", kTriangleObj);
    temp.Write("tri.obj.meta", R"({ "guid": "00000000-0000-0000-0000-0000000000ab", "type": "Texture", "importerVersion": 1, "settings": {} })");
    FakeGpuResourceManager gpu;

    const LogCounter warnings(LogLevel::Warn, "says it is a Texture, but its importer makes Mesh");
    AssetManager assets(gpu, temp.Path());

    CHECK(warnings.Count() == 1);

    const AssetInfo* info = assets.GetInfo(AssetID::FromString("00000000-0000-0000-0000-0000000000ab"));
    REQUIRE(info);
    CHECK(info->Path == "tri.obj");
    CHECK(info->Type.Is<MeshAsset>());
}

TEST_CASE("The version changes when assets come, go, move or load again", "[assets]")
{
    const TempFolder temp;
    temp.Write("a.mat", kValidMaterial);
    temp.Write("image.bmp", MakeImage());
    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());

    uint64_t version = assets.Version();
    const auto changed = [&]
    {
        const bool different = assets.Version() != version;
        version = assets.Version();
        return different;
    };

    const MaterialID material = assets.Find<MaterialAsset>("a.mat");
    const TextureID texture = assets.Find<TextureAsset>("image.bmp");

    SECTION("Not by looking things up, or by changing them in place")
    {
        REQUIRE(assets.Get(material));
        CHECK_FALSE(changed());

        MaterialData data = *assets.GetData(material);
        data.Properties["Color"] = { 1.0f, 0.0f, 0.0f, 1.0f };
        CHECK(assets.Apply(material, data) == AssetUpdate::Updated);
        CHECK_FALSE(changed());
    }

    SECTION("By what comes, moves and goes")
    {
        const MaterialID created = assets.Create<MaterialAsset>("b.mat");
        REQUIRE(created);
        CHECK(changed());

        REQUIRE(assets.Rename(created, "folder/b.mat"));
        CHECK(changed());

        REQUIRE(assets.Delete(created));
        CHECK(changed());

        temp.Write("c.mat", kValidMaterial);
        CHECK(assets.Refresh().empty());
        CHECK(changed());
    }

    SECTION("By loading one again")
    {
        REQUIRE(assets.Get(texture));
        CHECK_FALSE(changed());

        nlohmann::json settings = assets.GetInfo(texture)->Settings;
        settings["flipY"] = true;
        REQUIRE(assets.Reimport(texture, settings));
        CHECK(changed());
    }
}
