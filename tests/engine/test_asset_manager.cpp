#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/asset_manager/slot_materials.hpp>
#include <crimson/asset_manager/importers/texture_importer.hpp>
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
using crimson::tests::ResolveAssets;
using crimson::tests::TempFolder;

namespace fs = std::filesystem;

namespace
{
    constexpr std::string_view kTriangleObj = "v 0 0 0\nv 1 0 0\nv 0 2 0\nf 1 2 3\n";
    constexpr std::string_view kValidMaterial = R"({ "shader": "engine/shaders/S_Lit.hlsl" })";
    constexpr std::string_view kBrokenShader = "this does not compile";

    // A 1x1 uncompressed BMP, as PNGs are hard to write out by hand
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

    const MeshID mesh = assets.Find<AssetType::Mesh>("meshes/nested/tri.obj");
    REQUIRE(mesh);
    REQUIRE(assets.GetInfo(mesh));
    CHECK(assets.GetInfo(mesh)->Type == AssetType::Mesh);
    CHECK(assets.GetInfo(mesh)->Path == "meshes/nested/tri.obj");

    // The extension's case doesn't matter
    CHECK(assets.Find<AssetType::Texture>("textures/a.BMP"));

    CHECK_FALSE(assets.FindId("notes.txt"));
    CHECK_FALSE(assets.FindId("shaders/common.hlsli"));
    CHECK_FALSE(assets.Find<AssetType::Mesh>("no/such/asset.obj"));
    CHECK(assets.GetInfo(AssetID{}) == nullptr);
}

TEST_CASE("A folder that doesn't exist gives just the engine's assets", "[assets]")
{
    const TempFolder temp;
    FakeGpuResourceManager gpu;

    AssetManager assets(gpu, temp.Path() / "nothing");

    CHECK(assets.Find<AssetType::Material>("engine/materials/M_Default.mat"));
    CHECK(std::ranges::all_of(assets.List(AssetType::Mesh), [](const AssetInfo* info) { return info->ReadOnly && info->Path.starts_with("engine/"); }));
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

        const MeshID id = assets.Find<AssetType::Mesh>(expected.Path);
        REQUIRE(id);
        CHECK(assets.GetInfo(id)->ReadOnly);

        const Mesh* mesh = assets.Load(id);
        REQUIRE(mesh);
        CHECK(mesh->SubMeshes.size() == 1);
        CHECK(mesh->MaterialSlots.size() == 1);
        CHECK(near(mesh->Bounds.Min, expected.Min));
        CHECK(near(mesh->Bounds.Max, expected.Max));
    }

    CHECK(assets.List(AssetType::Mesh).size() == kMeshes.size());
}

TEST_CASE("An asset keeps its id through its meta file", "[assets]")
{
    const TempFolder temp;
    const fs::path file = temp.Write("tri.obj", kTriangleObj);
    FakeGpuResourceManager gpu;

    const AssetID first = AssetManager(gpu, temp.Path()).Find<AssetType::Mesh>("tri.obj");
    REQUIRE(first);

    CHECK(fs::exists(fs::path(file) += ".meta"));

    SECTION("When reopened")
    {
        CHECK(AssetManager(gpu, temp.Path()).Find<AssetType::Mesh>("tri.obj") == first);
    }

    SECTION("When the file is moved along with its meta")
    {
        fs::create_directories(temp.Path() / "moved");
        fs::rename(file, temp.Path() / "moved/tri.obj");
        fs::rename(fs::path(file) += ".meta", temp.Path() / "moved/tri.obj.meta");

        AssetManager assets(gpu, temp.Path());

        CHECK(assets.Find<AssetType::Mesh>("moved/tri.obj") == first);
        CHECK_FALSE(assets.Find<AssetType::Mesh>("tri.obj"));
    }

    SECTION("Unless the meta is lost")
    {
        fs::remove(fs::path(file) += ".meta");
        CHECK(AssetManager(gpu, temp.Path()).Find<AssetType::Mesh>("tri.obj") != first);
    }
}

TEST_CASE("A corrupt meta file gets replaced", "[assets]")
{
    const TempFolder temp;
    temp.Write("tri.obj", kTriangleObj);
    temp.Write("tri.obj.meta", "{ not json");
    FakeGpuResourceManager gpu;

    const AssetID id = AssetManager(gpu, temp.Path()).Find<AssetType::Mesh>("tri.obj");

    CHECK(id);
    CHECK(AssetManager(gpu, temp.Path()).Find<AssetType::Mesh>("tri.obj") == id);
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

    const MeshID a = assets.Find<AssetType::Mesh>("a.obj");
    const MeshID b = assets.Find<AssetType::Mesh>("b.obj");

    REQUIRE(a);
    REQUIRE(b);
    CHECK(a != b);
    CHECK(assets.GetInfo(a)->Path == "a.obj");
    CHECK(assets.GetInfo(b)->Path == "b.obj");

    // And it sticks
    AssetManager reopened(gpu, temp.Path());
    CHECK(reopened.Find<AssetType::Mesh>("a.obj") != reopened.Find<AssetType::Mesh>("b.obj"));
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
    for (const AssetInfo* info : assets.List(AssetType::Mesh))
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

    CHECK(DefaultMaterial(a) == a.Find<AssetType::Material>("engine/materials/M_Default.mat"));

    // Includes aren't assets, and engine assets don't write metas anywhere
    CHECK_FALSE(a.FindId("engine/shaders/crimson/bindings.hlsli"));
    CHECK(fs::is_empty(projectA.Path()));
}

TEST_CASE("Every engine material loads, without any project files", "[assets]")
{
    const TempFolder temp;
    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());

    const auto materials = assets.List(AssetType::Material);
    REQUIRE_FALSE(materials.empty());

    for (const AssetInfo* info : materials)
    {
        CAPTURE(info->Path);
        CHECK(assets.Load(assets.As<AssetType::Material>(info->Id)));
    }
}

TEST_CASE("A project can't shadow the engine's assets", "[assets]")
{
    const TempFolder temp;
    temp.Write("engine/materials/M_Default.mat", R"({ "shader": "nope" })");
    FakeGpuResourceManager gpu;

    AssetManager assets(gpu, temp.Path());

    CHECK(assets.Load<AssetType::Material>("engine/materials/M_Default.mat"));
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
        const MaterialHandle material = assets.Load<AssetType::Material>("mine.mat");
        REQUIRE(material);

        const auto& record = gpu.Materials.at(material.GetRaw());
        CHECK(record.Settings.RenderMode == MaterialRenderMode::Transparent);
        CHECK(record.Settings.Cull == CullMode::None);
        CHECK(record.Properties.at("Color").size() == sizeof(glm::vec4));
        CHECK(record.Textures.at("Albedo") == assets.Load<AssetType::Texture>("image.bmp"));
    }

    SECTION("Own shader, in a subfolder, using the engine's includes")
    {
        CHECK(assets.Load<AssetType::Material>("custom.mat"));
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

    const MaterialHandle first = assets.Load<AssetType::Material>("a.mat");
    CHECK(assets.Load<AssetType::Material>("a.mat") == first);
    CHECK(assets.Load(assets.Find<AssetType::Material>("a.mat")) == first);
    CHECK(gpu.LiveMaterials == 1);

    // Two materials share one shader
    CHECK(assets.Load<AssetType::Material>("b.mat"));
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
        CHECK_FALSE(assets.Load<AssetType::Shader>("broken.hlsl"));
        CHECK_FALSE(assets.Load<AssetType::Material>("uses_broken.mat"));
    }

    CHECK_FALSE(assets.Load<AssetType::Material>("also_uses_broken.mat"));

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
        CHECK_FALSE(assets.Load<AssetType::Material>("m.mat"));
    }

    SECTION("No shader")
    {
        temp.Write("m.mat", "{}");
        AssetManager assets(gpu, temp.Path());
        CHECK_FALSE(assets.Load<AssetType::Material>("m.mat"));
    }

    SECTION("A shader that doesn't exist")
    {
        temp.Write("m.mat", R"({ "shader": "nope.hlsl" })");
        AssetManager assets(gpu, temp.Path());
        CHECK_FALSE(assets.Load<AssetType::Material>("m.mat"));
    }

    SECTION("A missing texture only loses the texture")
    {
        temp.Write("m.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl", "textures": { "Albedo": "nope.bmp" } })");
        AssetManager assets(gpu, temp.Path());
        const MaterialHandle material = assets.Load<AssetType::Material>("m.mat");
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

    CHECK_FALSE(assets.Load(MaterialID{}));
    CHECK(assets.Load(MeshID{}) == nullptr);
    CHECK_FALSE(assets.Load<AssetType::Material>("no/such.mat"));

    SECTION("Asking for the wrong type doesn't load it as that")
    {
        CHECK_FALSE(assets.Load<AssetType::Material>("tri.obj"));
        CHECK_FALSE(assets.Load<AssetType::Texture>("tri.obj"));
        CHECK_FALSE(assets.Load<AssetType::Shader>("tri.obj"));
        CHECK(assets.Load<AssetType::Mesh>("tri.obj") != nullptr);
    }
}

TEST_CASE("Typed ids only come from assets of their type", "[assets]")
{
    const TempFolder temp;
    temp.Write("tri.obj", kTriangleObj);
    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());

    const MeshID untyped = assets.Find<AssetType::Mesh>("tri.obj");
    const MeshID mesh = assets.Find<AssetType::Mesh>("tri.obj");

    CHECK(mesh);
    CHECK(mesh == assets.As<AssetType::Mesh>(untyped));
    CHECK(static_cast<AssetID>(mesh) == untyped);
    CHECK(assets.Load(mesh) != nullptr);
    CHECK(assets.Load<AssetType::Mesh>("tri.obj") == assets.Load(mesh));

    SECTION("Another type gives an empty id")
    {
        CHECK_FALSE(assets.As<AssetType::Texture>(untyped));
        CHECK_FALSE(assets.Find<AssetType::Material>("tri.obj"));
        CHECK_FALSE(assets.Load<AssetType::Material>("tri.obj"));
    }

    SECTION("So does an id or path that doesn't exist")
    {
        CHECK_FALSE(assets.As<AssetType::Mesh>(AssetID{ 1234 }));
        CHECK_FALSE(assets.Find<AssetType::Mesh>("no/such.obj"));
        CHECK(assets.Load(MeshID{}) == nullptr);
    }

    SECTION("Engine assets have types too")
    {
        const ShaderID shader = assets.Find<AssetType::Shader>("engine/shaders/S_Lit.hlsl");
        REQUIRE(shader);
        CHECK(assets.Load(shader));
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

        const Mesh* mesh = assets.Load<AssetType::Mesh>("tri.obj");
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

        const Mesh* mesh = assets.Load<AssetType::Mesh>("tri.obj");
        REQUIRE(mesh);
        CHECK(mesh->MaterialSlots.size() == 2);
        REQUIRE(mesh->SubMeshes.size() == 1);
        CHECK(mesh->MaterialSlots[mesh->SubMeshes[0].MaterialSlot] == "blue");
    }

    SECTION("A file without geometry fails")
    {
        temp.Write("empty.obj", "# nothing here\n");
        AssetManager assets(gpu, temp.Path());
        CHECK(assets.Load<AssetType::Mesh>("empty.obj") == nullptr);
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

    const Mesh* mesh = assets.Load<AssetType::Mesh>("tri.obj");
    REQUIRE(mesh);
    CHECK(mesh->MaterialSlots == std::vector<std::string>{ "red" });
    CHECK(assets.Load<AssetType::Texture>("image.bmp"));
    CHECK(assets.Load<AssetType::Material>("own.mat"));
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
    const TextureID id = assets.Find<AssetType::Texture>("image.bmp");

    REQUIRE(assets.Load(id));
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
        CHECK(assets.Load<AssetType::Mesh>("tri.obj"));
        CHECK(assets.Load<AssetType::Material>("mine.mat"));

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

TEST_CASE("A mesh's empty slots use the materials made for them", "[assets]")
{
    const TempFolder temp;
    temp.Write("model/two.obj", "mtllib two.mtl\nv 0 0 0\nv 1 0 0\nv 0 2 0\nv 0 0 1\nusemtl stone\nf 1 2 3\nusemtl wood\nf 1 2 4\n");
    temp.Write("model/two.mtl", "newmtl stone\nnewmtl wood\n");
    temp.Write("model/materials/stone.mat", kValidMaterial);
    temp.Write("materials/wood.mat", kValidMaterial); // not next to this mesh
    temp.Write("mine.mat", kValidMaterial);

    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());

    const MeshID mesh = assets.Find<AssetType::Mesh>("model/two.obj");
    const MaterialID stone = assets.Find<AssetType::Material>("model/materials/stone.mat");

    CHECK(FindSlotMaterial(assets, mesh, "stone") == stone);
    CHECK_FALSE(FindSlotMaterial(assets, mesh, "wood"));
    CHECK_FALSE(FindSlotMaterial(assets, MeshID{}, "stone"));

    MeshRendererComponent renderer;
    renderer.MeshAsset = mesh;

    SECTION("Without one it is the default material")
    {
        ResolveAssets(assets, renderer);

        REQUIRE(renderer.MaterialAssets.size() == 2);
        CHECK(renderer.MaterialAssets[0] == stone);
        CHECK(renderer.MaterialAssets[1] == DefaultMaterial(assets));
        CHECK(renderer.Resolved.Materials[0] == assets.Load(stone));
    }

    SECTION("A chosen material wins")
    {
        renderer.MaterialAssets = { assets.Find<AssetType::Material>("mine.mat") };
        ResolveAssets(assets, renderer);

        REQUIRE(renderer.MaterialAssets.size() == 2);
        CHECK(renderer.MaterialAssets[0] == assets.Find<AssetType::Material>("mine.mat"));
    }
}

TEST_CASE("Resolving a mesh renderer fills in what is missing", "[assets]")
{
    const TempFolder temp;
    temp.Write("tri.obj", kTriangleObj);
    temp.Write("mine.mat", kValidMaterial);
    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());

    MeshRendererComponent renderer;

    SECTION("No mesh gives nothing to draw")
    {
        ResolveAssets(assets, renderer);
        CHECK(renderer.Resolved.SubMeshes.empty());
    }

    SECTION("An empty slot gets the default material")
    {
        renderer.MeshAsset = assets.Find<AssetType::Mesh>("tri.obj");
        ResolveAssets(assets, renderer);

        REQUIRE(renderer.MaterialAssets.size() == 1);
        CHECK(renderer.MaterialAssets[0] == DefaultMaterial(assets));
        CHECK(renderer.Resolved.Materials[0] == assets.Load(DefaultMaterial(assets)));
    }

    SECTION("A chosen material is kept")
    {
        renderer.MeshAsset = assets.Find<AssetType::Mesh>("tri.obj");
        renderer.MaterialAssets = { assets.Find<AssetType::Material>("mine.mat") };
        ResolveAssets(assets, renderer);

        CHECK(renderer.Resolved.Materials[0] == assets.Load<AssetType::Material>("mine.mat"));
    }

    SECTION("Extra slots are dropped, as the mesh decides how many it has")
    {
        renderer.MeshAsset = assets.Find<AssetType::Mesh>("tri.obj");
        renderer.MaterialAssets = { assets.Find<AssetType::Material>("mine.mat"), assets.Find<AssetType::Material>("mine.mat"), assets.Find<AssetType::Material>("mine.mat") };
        ResolveAssets(assets, renderer);

        CHECK(renderer.MaterialAssets.size() == 1);
    }

    SECTION("Resolving again clears the old result when the mesh is removed")
    {
        renderer.MeshAsset = assets.Find<AssetType::Mesh>("tri.obj");
        ResolveAssets(assets, renderer);
        REQUIRE_FALSE(renderer.Resolved.SubMeshes.empty());

        renderer.MeshAsset = {};
        ResolveAssets(assets, renderer);
        CHECK(renderer.Resolved.SubMeshes.empty());
    }
}

namespace
{
    std::string ReadFile(const fs::path& file)
    {
        std::ifstream stream(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }

    // The id an engine asset had at a path, FNV-1a of it
    std::string EngineIdOf(std::string_view path)
    {
        uint64_t hash = 0xcbf29ce484222325ull;
        for (const unsigned char c : path)
        {
            hash ^= c;
            hash *= 0x100000001b3ull;
        }
        return AssetID{ hash }.ToString();
    }
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
            const auto data = ParseMaterialData(text, assets, "test");
            REQUIRE(data);
            CHECK(data->Shader == lit);
            CHECK_THAT(WriteMaterialData(*data, assets), Catch::Matchers::ContainsSubstring(R"("path": "engine/shaders/S_Lit.hlsl")"));
        }
    }

    SECTION("A scene")
    {
        const std::string text = R"({ "version": 1, "entities": [ { "meshRenderer": { "mesh": { "guid": ")" + EngineIdOf("engine/meshes/cube.obj")
            + R"(", "path": "engine/meshes/cube.obj" }, "materials": [ "engine/materials/default.mat" ] } } ] })";

        const auto data = ParseSceneData(text, assets, "test");
        REQUIRE(data);
        CHECK(FieldOf<AssetID>(assets, data->Roots.at(0), "meshRenderer", "mesh") == cube);
        CHECK(FieldOf<std::vector<AssetID>>(assets, data->Roots.at(0), "meshRenderer", "materials").at(0) == DefaultMaterial(assets));

        const std::string written = WriteSceneData(*data, assets);
        CHECK_THAT(written, Catch::Matchers::ContainsSubstring("engine/meshes/SM_Cube.obj"));
        CHECK_THAT(written, Catch::Matchers::ContainsSubstring("engine/materials/M_Default.mat"));
    }

    SECTION("An id that was never an engine asset's stays as it is")
    {
        CHECK(assets.Redirect(AssetID{ 42 }) == AssetID{ 42 });
        CHECK(assets.Redirect(lit) == lit);
    }
}

TEST_CASE("The renderer's own shaders and materials are internal", "[assets]")
{
    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, fs::path("does/not/exist"));

    for (const std::string_view path : { "engine/shaders/internal/S_ToneMap.hlsl", "engine/materials/internal/M_Outline.mat", "engine/scenes/internal/SC_NewScene.scene" })
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
