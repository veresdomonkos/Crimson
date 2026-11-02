#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/asset_manager/importers/texture_importer.hpp>
#include <crimson/core/log.hpp>
#include <crimson/scene/scene_renderer.hpp>

#include "support/fake_gpu.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using crimson::tests::FakeGpuResourceManager;
using crimson::tests::TempFolder;

namespace fs = std::filesystem;

namespace
{
    constexpr std::string_view kTriangleObj = "v 0 0 0\nv 1 0 0\nv 0 2 0\nf 1 2 3\n";
    constexpr std::string_view kValidMaterial = R"({ "shader": "engine/shaders/main.hlsl" })";
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

    // Counts logged errors that mention a text, to tell apart "failed once" from "fails on every request"
    class ErrorCounter
    {
    public:
        explicit ErrorCounter(std::string text)
            : m_text(std::move(text))
        {
            m_id = Logger::Subscribe([this](LogLevel level, const std::string& message)
            {
                if (m_started && level == LogLevel::Error && message.find(m_text) != std::string::npos)
                    ++m_count;
            });
            m_started = true;
        }

        ~ErrorCounter() { Logger::Unsubscribe(m_id); }

        [[nodiscard]] int Count() const { return m_count; }
    private:
        std::string m_text;
        uint64_t m_id = 0;
        bool m_started = false;
        int m_count = 0;
    };
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

    const AssetID mesh = assets.FindId("meshes/nested/tri.obj");
    REQUIRE(mesh);
    REQUIRE(assets.GetInfo(mesh));
    CHECK(assets.GetInfo(mesh)->Type == AssetType::Mesh);
    CHECK(assets.GetInfo(mesh)->Path == "meshes/nested/tri.obj");

    // The extension's case doesn't matter
    CHECK(assets.FindId("textures/a.BMP"));

    CHECK_FALSE(assets.FindId("notes.txt"));
    CHECK_FALSE(assets.FindId("shaders/common.hlsli"));
    CHECK_FALSE(assets.FindId("no/such/asset.obj"));
    CHECK(assets.GetInfo(AssetID{}) == nullptr);
}

TEST_CASE("A folder that doesn't exist gives just the engine's assets", "[assets]")
{
    const TempFolder temp;
    FakeGpuResourceManager gpu;

    AssetManager assets(gpu, temp.Path() / "nothing");

    CHECK(assets.FindId("engine/materials/default.mat"));
    CHECK(assets.List(AssetType::Mesh).empty());
}

TEST_CASE("An asset keeps its id through its meta file", "[assets]")
{
    const TempFolder temp;
    const fs::path file = temp.Write("tri.obj", kTriangleObj);
    FakeGpuResourceManager gpu;

    const AssetID first = AssetManager(gpu, temp.Path()).FindId("tri.obj");
    REQUIRE(first);

    CHECK(fs::exists(fs::path(file) += ".meta"));

    SECTION("When reopened")
    {
        CHECK(AssetManager(gpu, temp.Path()).FindId("tri.obj") == first);
    }

    SECTION("When the file is moved along with its meta")
    {
        fs::create_directories(temp.Path() / "moved");
        fs::rename(file, temp.Path() / "moved/tri.obj");
        fs::rename(fs::path(file) += ".meta", temp.Path() / "moved/tri.obj.meta");

        AssetManager assets(gpu, temp.Path());

        CHECK(assets.FindId("moved/tri.obj") == first);
        CHECK_FALSE(assets.FindId("tri.obj"));
    }

    SECTION("Unless the meta is lost")
    {
        fs::remove(fs::path(file) += ".meta");
        CHECK(AssetManager(gpu, temp.Path()).FindId("tri.obj") != first);
    }
}

TEST_CASE("A corrupt meta file gets replaced", "[assets]")
{
    const TempFolder temp;
    temp.Write("tri.obj", kTriangleObj);
    temp.Write("tri.obj.meta", "{ not json");
    FakeGpuResourceManager gpu;

    const AssetID id = AssetManager(gpu, temp.Path()).FindId("tri.obj");

    CHECK(id);
    CHECK(AssetManager(gpu, temp.Path()).FindId("tri.obj") == id);
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

    const AssetID a = assets.FindId("a.obj");
    const AssetID b = assets.FindId("b.obj");

    REQUIRE(a);
    REQUIRE(b);
    CHECK(a != b);
    CHECK(assets.GetInfo(a)->Path == "a.obj");
    CHECK(assets.GetInfo(b)->Path == "b.obj");

    // And it sticks
    AssetManager reopened(gpu, temp.Path());
    CHECK(reopened.FindId("a.obj") != reopened.FindId("b.obj"));
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

    std::vector<std::string> paths;
    for (const AssetInfo* info : assets.List(AssetType::Mesh))
        paths.push_back(info->Path);

    CHECK(paths == std::vector<std::string>{ "a.obj", "m/b.obj", "z.obj" });
}

TEST_CASE("Engine assets are always there, with the same id everywhere", "[assets]")
{
    const TempFolder projectA;
    const TempFolder projectB;
    FakeGpuResourceManager gpu;

    AssetManager a(gpu, projectA.Path());
    AssetManager b(gpu, projectB.Path());

    for (const char* path : { "engine/materials/default.mat", "engine/materials/shadow.mat", "engine/materials/outline.mat",
                              "engine/materials/outline_mask.mat", "engine/shaders/main.hlsl" })
    {
        CAPTURE(path);
        CHECK(a.FindId(path));
        CHECK(a.FindId(path) == b.FindId(path));
    }

    CHECK(a.DefaultMaterial() == a.FindId("engine/materials/default.mat"));

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
        CHECK(assets.LoadMaterial(info->Id));
    }
}

TEST_CASE("A project can't shadow the engine's assets", "[assets]")
{
    const TempFolder temp;
    temp.Write("engine/materials/default.mat", R"({ "shader": "nope" })");
    FakeGpuResourceManager gpu;

    AssetManager assets(gpu, temp.Path());

    CHECK(assets.LoadMaterial("engine/materials/default.mat"));
    CHECK_FALSE(fs::exists(temp.Path() / "engine/materials/default.mat.meta"));
}

TEST_CASE("A project's material can use an engine shader, or its own", "[assets]")
{
    const TempFolder temp;
    temp.Write("mine.mat", R"({
        "shader": "engine/shaders/main.hlsl",
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
        const MaterialHandle material = assets.LoadMaterial("mine.mat");
        REQUIRE(material);

        const auto& record = gpu.Materials.at(material.GetRaw());
        CHECK(record.Settings.RenderMode == MaterialRenderMode::Transparent);
        CHECK(record.Settings.Cull == CullMode::None);
        CHECK(record.Properties.at("Color").size() == sizeof(glm::vec4));
        CHECK(record.Textures.at("Albedo") == assets.LoadTexture("image.bmp"));
    }

    SECTION("Own shader, in a subfolder, using the engine's includes")
    {
        CHECK(assets.LoadMaterial("custom.mat"));
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

    const MaterialHandle first = assets.LoadMaterial("a.mat");
    CHECK(assets.LoadMaterial("a.mat") == first);
    CHECK(assets.LoadMaterial(assets.FindId("a.mat")) == first);
    CHECK(gpu.LiveMaterials == 1);

    // Two materials share one shader
    CHECK(assets.LoadMaterial("b.mat"));
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
    const ErrorCounter errors("Compilation failed");

    for (int i = 0; i < 3; ++i)
    {
        CHECK_FALSE(assets.LoadShader("broken.hlsl"));
        CHECK_FALSE(assets.LoadMaterial("uses_broken.mat"));
    }

    CHECK_FALSE(assets.LoadMaterial("also_uses_broken.mat"));

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
        CHECK_FALSE(assets.LoadMaterial("m.mat"));
    }

    SECTION("No shader")
    {
        temp.Write("m.mat", "{}");
        AssetManager assets(gpu, temp.Path());
        CHECK_FALSE(assets.LoadMaterial("m.mat"));
    }

    SECTION("A shader that doesn't exist")
    {
        temp.Write("m.mat", R"({ "shader": "nope.hlsl" })");
        AssetManager assets(gpu, temp.Path());
        CHECK_FALSE(assets.LoadMaterial("m.mat"));
    }

    SECTION("A missing texture only loses the texture")
    {
        temp.Write("m.mat", R"({ "shader": "engine/shaders/main.hlsl", "textures": { "Albedo": "nope.bmp" } })");
        AssetManager assets(gpu, temp.Path());
        const MaterialHandle material = assets.LoadMaterial("m.mat");
        REQUIRE(material);
        CHECK(gpu.Materials.at(material.GetRaw()).Textures.empty());
    }
}

TEST_CASE("Empty and mistyped requests get nothing", "[assets]")
{
    const TempFolder temp;
    temp.Write("tri.obj", kTriangleObj);
    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());

    CHECK_FALSE(assets.LoadMaterial(AssetID{}));
    CHECK(assets.LoadMesh(AssetID{}) == nullptr);
    CHECK_FALSE(assets.LoadMaterial("no/such.mat"));

    SECTION("Asking for the wrong type doesn't load it as that")
    {
        CHECK_FALSE(assets.LoadMaterial("tri.obj"));
        CHECK_FALSE(assets.LoadTexture("tri.obj"));
        CHECK_FALSE(assets.LoadShader("tri.obj"));
        CHECK(assets.LoadMesh("tri.obj") != nullptr);
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

        const Mesh* mesh = assets.LoadMesh("tri.obj");
        REQUIRE(mesh);
        CHECK(mesh->VB);
        CHECK(mesh->IB);
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

        const Mesh* mesh = assets.LoadMesh("tri.obj");
        REQUIRE(mesh);
        CHECK(mesh->MaterialSlots.size() == 2);
        REQUIRE(mesh->SubMeshes.size() == 1);
        CHECK(mesh->MaterialSlots[mesh->SubMeshes[0].MaterialSlot] == "blue");
    }

    SECTION("A file without geometry fails")
    {
        temp.Write("empty.obj", "# nothing here\n");
        AssetManager assets(gpu, temp.Path());
        CHECK(assets.LoadMesh("empty.obj") == nullptr);
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

    const Mesh* mesh = assets.LoadMesh("tri.obj");
    REQUIRE(mesh);
    CHECK(mesh->MaterialSlots == std::vector<std::string>{ "red" });
    CHECK(assets.LoadTexture("image.bmp"));
    CHECK(assets.LoadMaterial("own.mat"));
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
    const AssetID id = assets.FindId("image.bmp");

    REQUIRE(assets.LoadTexture(id));
    CHECK(gpu.LastTextureInfo.Format == TextureFormat::RGBA8Srgb);

    SECTION("Turning it off")
    {
        REQUIRE(assets.ReimportTexture(id, { { "srgb", false } }));
        CHECK(gpu.LastTextureInfo.Format == TextureFormat::RGBA8);
    }

    SECTION("A preview shows what is stored, and belongs to the caller")
    {
        const int before = gpu.LiveTextures;

        const TextureHandle preview = assets.CreateTexturePreview(id);
        REQUIRE(preview);
        CHECK(gpu.LastTextureInfo.Format == TextureFormat::RGBA8);
        CHECK(gpu.LiveTextures == before + 1);

        assets.DestroyTexturePreview(preview);
        CHECK(gpu.LiveTextures == before);
    }

    SECTION("Only textures have a preview")
    {
        CHECK_FALSE(assets.CreateTexturePreview(assets.FindId("engine/materials/default.mat")));
    }
}

TEST_CASE("Destroying the manager frees what it loaded, users first", "[assets]")
{
    const TempFolder temp;
    temp.Write("tri.obj", kTriangleObj);
    temp.Write("image.bmp", MakeImage());
    temp.Write("mine.mat", R"({ "shader": "engine/shaders/main.hlsl", "textures": { "Albedo": "image.bmp" } })");

    FakeGpuResourceManager gpu;

    {
        AssetManager assets(gpu, temp.Path());
        CHECK(assets.LoadMesh("tri.obj"));
        CHECK(assets.LoadMaterial("mine.mat"));

        CHECK(gpu.LiveMaterials == 1);
        CHECK(gpu.LiveShaders == 1);
        CHECK(gpu.LiveTextures == 1);
    }

    CHECK(gpu.NothingAlive());

    // A material can't outlive the shader it was made from
    const auto firstShader = std::ranges::find(gpu.DestroyOrder, "shader");
    const auto lastMaterial = std::ranges::find_last(gpu.DestroyOrder, "material").begin();
    CHECK(lastMaterial < firstShader);
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
        ResolveMeshRenderer(renderer, assets);
        CHECK(renderer.Resolved.SubMeshes.empty());
    }

    SECTION("An empty slot gets the default material")
    {
        renderer.MeshAsset = assets.FindId("tri.obj");
        ResolveMeshRenderer(renderer, assets);

        REQUIRE(renderer.MaterialAssets.size() == 1);
        CHECK(renderer.MaterialAssets[0] == assets.DefaultMaterial());
        CHECK(renderer.Resolved.Materials[0] == assets.LoadMaterial(assets.DefaultMaterial()));
    }

    SECTION("A chosen material is kept")
    {
        renderer.MeshAsset = assets.FindId("tri.obj");
        renderer.MaterialAssets = { assets.FindId("mine.mat") };
        ResolveMeshRenderer(renderer, assets);

        CHECK(renderer.Resolved.Materials[0] == assets.LoadMaterial("mine.mat"));
    }

    SECTION("Extra slots are dropped, as the mesh decides how many it has")
    {
        renderer.MeshAsset = assets.FindId("tri.obj");
        renderer.MaterialAssets = { assets.FindId("mine.mat"), assets.FindId("mine.mat"), assets.FindId("mine.mat") };
        ResolveMeshRenderer(renderer, assets);

        CHECK(renderer.MaterialAssets.size() == 1);
    }

    SECTION("Resolving again clears the old result when the mesh is removed")
    {
        renderer.MeshAsset = assets.FindId("tri.obj");
        ResolveMeshRenderer(renderer, assets);
        REQUIRE_FALSE(renderer.Resolved.SubMeshes.empty());

        renderer.MeshAsset = {};
        ResolveMeshRenderer(renderer, assets);
        CHECK(renderer.Resolved.SubMeshes.empty());
    }
}
