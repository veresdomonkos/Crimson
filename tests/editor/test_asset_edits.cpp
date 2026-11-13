#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <crimson/asset_manager/slot_materials.hpp>
#include <crimson_editor/asset_edits.hpp>

#include "support/fake_gpu.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using crimson::editor::AssetEdits;
using crimson::tests::FakeGpuResourceManager;
using crimson::tests::TempFolder;

namespace
{
    std::string ReadFile(const std::filesystem::path& file)
    {
        std::ifstream stream(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }

    struct Fixture
    {
        TempFolder Temp;
        FakeGpuResourceManager Gpu;
        std::filesystem::path File = Temp.Write("mine.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl", "properties": { "Color": [1, 0, 0, 1] } })");
        AssetManager Assets{ Gpu, Temp.Path() };
        AssetEdits Edits{ Assets };
        MaterialID Id = Assets.Find<AssetType::Material>("mine.mat");

        MaterialData MakeChanged()
        {
            MaterialData data = *Edits.GetMaterial(Id);
            data.Properties["Color"] = { 0.0f, 1.0f, 0.0f, 1.0f };
            return data;
        }
    };
}

TEST_CASE_METHOD(Fixture, "Looking at a material doesn't change anything", "[asset edits]")
{
    REQUIRE(Edits.GetMaterial(Id));

    CHECK_FALSE(Edits.IsDirty(Id));
    CHECK_FALSE(Edits.HasUnsavedChanges());
}

TEST_CASE_METHOD(Fixture, "An edit is unsaved until it is saved", "[asset edits]")
{
    const std::string before = ReadFile(File);
    const MaterialData changed = MakeChanged();

    Edits.SetMaterial(Id, changed);

    CHECK(Edits.IsDirty(Id));
    CHECK(Edits.HasUnsavedChanges());
    CHECK(*Edits.GetMaterial(Id) == changed);
    CHECK(ReadFile(File) == before);

    SECTION("Saving writes the file and clears it")
    {
        REQUIRE(Edits.Save(Id));

        CHECK_FALSE(Edits.IsDirty(Id));
        CHECK(ReadFile(File) != before);
        CHECK(AssetManager(Gpu, Temp.Path()).Read(Id)->Properties.at("Color") == changed.Properties.at("Color"));
    }

    SECTION("Saving everything does the same")
    {
        REQUIRE(Edits.SaveAll());
        CHECK_FALSE(Edits.HasUnsavedChanges());
    }

    SECTION("Reverting goes back to the file")
    {
        Edits.Revert(Id);

        CHECK_FALSE(Edits.IsDirty(Id));
        CHECK(Edits.GetMaterial(Id)->Properties.at("Color") == std::vector{ 1.0f, 0.0f, 0.0f, 1.0f });
        CHECK(ReadFile(File) == before);
    }

    SECTION("Discarding everything does the same")
    {
        Edits.DiscardAll();
        CHECK_FALSE(Edits.HasUnsavedChanges());
    }

    SECTION("Changing it back by hand means nothing is unsaved")
    {
        MaterialData original = changed;
        original.Properties["Color"] = { 1.0f, 0.0f, 0.0f, 1.0f };
        Edits.SetMaterial(Id, original);

        CHECK_FALSE(Edits.IsDirty(Id));
    }

    SECTION("Forgetting an asset that is gone drops its edit")
    {
        Edits.Forget(Id);
        CHECK_FALSE(Edits.HasUnsavedChanges());
    }
}

TEST_CASE_METHOD(Fixture, "Edits reach the GPU when they are applied", "[asset edits]")
{
    const MaterialHandle handle = Assets.Load(Id);
    REQUIRE(handle);

    MaterialData changed = MakeChanged();
    changed.Settings.Cull = CullMode::None;
    Edits.SetMaterial(Id, changed);

    // Panels make the edit in the middle of a frame, when the GPU's copy has to stay as it is
    CHECK(Gpu.Materials.at(handle.GetRaw()).Settings.Cull == CullMode::Back);

    CHECK_FALSE(Edits.Apply());
    CHECK(Gpu.Materials.at(handle.GetRaw()).Settings.Cull == CullMode::None);

    SECTION("Reverting shows the file's version again")
    {
        Edits.Revert(Id);
        CHECK_FALSE(Edits.Apply());
        CHECK(Gpu.Materials.at(handle.GetRaw()).Settings.Cull == CullMode::Back);
    }
}

TEST_CASE_METHOD(Fixture, "Another shader asks for handles to be got again", "[asset edits]")
{
    Temp.Write("own.hlsl", R"(
#include "crimson/bindings.hlsli"
float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }
float4 PSMain() : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }
)");
    Assets.Refresh();

    MaterialData changed = *Edits.GetMaterial(Id);
    changed.Shader = Assets.Find<AssetType::Shader>("own.hlsl");
    Edits.SetMaterial(Id, changed);

    CHECK(Edits.Apply());

    // It only happens once, not on every call
    CHECK_FALSE(Edits.Apply());
}

TEST_CASE_METHOD(Fixture, "An edit that can't be shown is undone", "[asset edits]")
{
    Temp.Write("broken.hlsl", "this does not compile");
    Assets.Refresh();

    MaterialData changed = *Edits.GetMaterial(Id);
    changed.Shader = Assets.Find<AssetType::Shader>("broken.hlsl");
    Edits.SetMaterial(Id, changed);

    CHECK_FALSE(Edits.Apply());

    CHECK(Edits.GetMaterial(Id)->Shader == Assets.Find<AssetType::Shader>("engine/shaders/S_Lit.hlsl"));
    CHECK_FALSE(Edits.IsDirty(Id));
}

TEST_CASE_METHOD(Fixture, "Engine materials can't be edited", "[asset edits]")
{
    const MaterialID engine = DefaultMaterial(Assets);

    MaterialData changed = *Edits.GetMaterial(engine);
    changed.Settings.Cull = CullMode::None;
    Edits.SetMaterial(engine, changed);

    CHECK_FALSE(Edits.IsDirty(engine));
    CHECK(Edits.GetMaterial(engine)->Settings.Cull == CullMode::Back);
}
