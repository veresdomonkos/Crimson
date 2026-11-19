#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <crimson/asset_manager/slot_materials.hpp>
#include <crimson/asset_manager/importers/material_asset.hpp>
#include <crimson/asset_manager/importers/shader_asset.hpp>
#include <crimson/asset_manager/importers/texture_asset.hpp>

#include <crimson/scene/entity.hpp>
#include <crimson_editor/asset_edits.hpp>
#include <crimson_editor/edit_history.hpp>
#include <crimson_editor/scene_edits.hpp>

#include "support/fake_gpu.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using crimson::editor::AssetEdits;
using crimson::editor::EditHistory;
using crimson::editor::SceneEdits;
using crimson::tests::FakeGpuResourceManager;
using crimson::tests::TempFolder;

namespace
{
    std::string ReadFile(const std::filesystem::path& file)
    {
        std::ifstream stream(file, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }

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

    struct Project
    {
        TempFolder Temp;
        FakeGpuResourceManager Gpu;
        std::filesystem::path File = Temp.Write("mine.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl", "properties": { "Color": [1, 0, 0, 1] } })");
        std::filesystem::path Picture = Temp.Write("picture.bmp", MakeImage());
        AssetManager Assets{ Gpu, Temp.Path() };
        AssetEdits Edits{ Assets };
        EditHistory History;
        MaterialID Id = Assets.Find<MaterialAsset>("mine.mat");

        MaterialData MakeChanged()
        {
            MaterialData data = *Edits.Get(Id);
            data.Properties["Color"] = { 0.0f, 1.0f, 0.0f, 1.0f };
            return data;
        }

        [[nodiscard]] CullMode GpuCull(MaterialHandle handle) const { return Gpu.Materials.at(handle.GetRaw()).Settings.Cull; }
    };

    struct Fixture : Project
    {
        Fixture() { History.AddSource(Edits); }
    };

    // With the scene's edits too, which are collected first
    struct SceneFixture : Project
    {
        Scene World;
        SceneSettings Settings;
        SceneEdits SceneUndo{ World, Assets, Settings, [] {} };

        SceneFixture()
        {
            History.AddSource(SceneUndo);
            History.AddSource(Edits);
        }
    };
}

TEST_CASE_METHOD(Fixture, "Looking at a material doesn't change anything", "[asset edits]")
{
    REQUIRE(Edits.Get(Id));

    CHECK_FALSE(Edits.IsDirty(Id));
    CHECK_FALSE(Edits.HasUnsavedChanges());
}

TEST_CASE_METHOD(Fixture, "An edit is unsaved until it is saved", "[asset edits]")
{
    const std::string before = ReadFile(File);
    const MaterialData changed = MakeChanged();

    Edits.Set(Id, changed);

    CHECK(Edits.IsDirty(Id));
    CHECK(Edits.HasUnsavedChanges());
    CHECK(*Edits.Get(Id) == changed);
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

    SECTION("Discarding everything goes back to the file")
    {
        Edits.DiscardAll();

        CHECK_FALSE(Edits.HasUnsavedChanges());
        CHECK(Edits.Get(Id)->Properties.at("Color") == std::vector{ 1.0f, 0.0f, 0.0f, 1.0f });
        CHECK(ReadFile(File) == before);
    }

    SECTION("Changing it back by hand means nothing is unsaved")
    {
        MaterialData original = changed;
        original.Properties["Color"] = { 1.0f, 0.0f, 0.0f, 1.0f };
        Edits.Set(Id, original);

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
    Edits.Set(Id, changed);

    // Panels make the edit in the middle of a frame, when the GPU's copy has to stay as it is
    CHECK(GpuCull(handle) == CullMode::Back);

    CHECK_FALSE(Edits.Apply());
    CHECK(GpuCull(handle) == CullMode::None);

    SECTION("Discarding everything shows the file's version again")
    {
        Edits.DiscardAll();
        CHECK_FALSE(Edits.Apply());
        CHECK(GpuCull(handle) == CullMode::Back);
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

    MaterialData changed = *Edits.Get(Id);
    changed.Shader = Assets.Find<ShaderAsset>("own.hlsl");
    Edits.Set(Id, changed);

    CHECK(Edits.Apply());

    // It only happens once, not on every call
    CHECK_FALSE(Edits.Apply());
}

TEST_CASE_METHOD(Fixture, "An edit that can't be shown is undone", "[asset edits]")
{
    Temp.Write("broken.hlsl", "this does not compile");
    Assets.Refresh();

    MaterialData changed = *Edits.Get(Id);
    changed.Shader = Assets.Find<ShaderAsset>("broken.hlsl");
    Edits.Set(Id, changed);

    CHECK_FALSE(Edits.Apply());

    CHECK(Edits.Get(Id)->Shader == Assets.Find<ShaderAsset>("engine/shaders/S_Lit.hlsl"));
    CHECK_FALSE(Edits.IsDirty(Id));

    // Nothing is left of it to undo
    CHECK_FALSE(History.Commit());
}

TEST_CASE_METHOD(Fixture, "Engine materials can't be edited", "[asset edits]")
{
    const MaterialID engine = slot_materials::DefaultMaterial(Assets);

    MaterialData changed = *Edits.Get(engine);
    changed.Settings.Cull = CullMode::None;
    Edits.Set(engine, changed);

    CHECK_FALSE(Edits.IsDirty(engine));
    CHECK(Edits.Get(engine)->Settings.Cull == CullMode::Back);
    CHECK_FALSE(History.Commit());
}

TEST_CASE_METHOD(Fixture, "A material property change is undone and redone", "[asset edits]")
{
    const MaterialHandle handle = Assets.Load(Id);
    const MaterialData original = *Edits.Get(Id);

    MaterialData changed = MakeChanged();
    changed.Settings.Cull = CullMode::None;
    Edits.Set(Id, changed);
    REQUIRE(History.Commit());
    Edits.Apply();

    REQUIRE(History.Undo());
    CHECK(*Edits.Get(Id) == original);
    CHECK_FALSE(Edits.IsDirty(Id));

    // Undoing shows on the GPU when applied, like an edit
    CHECK(GpuCull(handle) == CullMode::None);
    Edits.Apply();
    CHECK(GpuCull(handle) == CullMode::Back);

    REQUIRE(History.Redo());
    Edits.Apply();
    CHECK(*Edits.Get(Id) == changed);
    CHECK(Edits.IsDirty(Id));
    CHECK(GpuCull(handle) == CullMode::None);
}

TEST_CASE_METHOD(Fixture, "A setting and a texture are undone", "[asset edits]")
{
    const MaterialData original = *Edits.Get(Id);

    MaterialData changed = original;
    changed.Settings.Cull = CullMode::None;
    changed.Textures["ColorMap"] = Assets.Find<TextureAsset>("picture.bmp");
    Edits.Set(Id, changed);
    REQUIRE(History.Commit());

    REQUIRE(History.Undo());
    CHECK(Edits.Get(Id)->Settings.Cull == original.Settings.Cull);
    CHECK_FALSE(Edits.Get(Id)->Textures.contains("ColorMap"));

    REQUIRE(History.Redo());
    CHECK(*Edits.Get(Id) == changed);
}

TEST_CASE_METHOD(Fixture, "Many sets before a commit are one step", "[asset edits]")
{
    const MaterialData original = *Edits.Get(Id);

    // Like a slider being dragged
    for (const float green : { 0.2f, 0.5f, 0.8f })
    {
        MaterialData data = original;
        data.Properties["Color"] = { 0.0f, green, 0.0f, 1.0f };
        Edits.Set(Id, data);
    }

    REQUIRE(History.Commit());

    REQUIRE(History.Undo());
    CHECK(*Edits.Get(Id) == original);
    CHECK_FALSE(History.CanUndo());
}

TEST_CASE_METHOD(Fixture, "Setting it back before the step ends is no step", "[asset edits]")
{
    const MaterialData original = *Edits.Get(Id);

    Edits.Set(Id, MakeChanged());
    Edits.Set(Id, original);

    CHECK_FALSE(History.Commit());
    CHECK_FALSE(History.CanUndo());
}

TEST_CASE_METHOD(Fixture, "Undoing back to the saved material clears its mark, and past a save sets it again", "[asset edits]")
{
    Edits.Set(Id, MakeChanged());
    REQUIRE(History.Commit());
    REQUIRE(Edits.Save(Id));
    CHECK_FALSE(Edits.IsDirty(Id));

    REQUIRE(History.Undo());
    CHECK(Edits.IsDirty(Id));

    REQUIRE(History.Redo());
    CHECK_FALSE(Edits.IsDirty(Id));
}

TEST_CASE_METHOD(Fixture, "A shader change is undone through Apply and asks for handles again", "[asset edits]")
{
    Temp.Write("own.hlsl", R"(
#include "crimson/bindings.hlsli"
float4 VSMain(float3 position : POSITION) : SV_Position { return float4(position, 1.0); }
float4 PSMain() : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }
)");
    Assets.Refresh();

    MaterialData changed = *Edits.Get(Id);
    changed.Shader = Assets.Find<ShaderAsset>("own.hlsl");
    Edits.Set(Id, changed);
    REQUIRE(History.Commit());
    REQUIRE(Edits.Apply());

    REQUIRE(History.Undo());
    CHECK(Edits.Apply());
    CHECK(Assets.GetData(Id)->Shader == Assets.Find<ShaderAsset>("engine/shaders/S_Lit.hlsl"));
}

TEST_CASE_METHOD(Fixture, "A step for a forgotten material is skipped", "[asset edits]")
{
    Edits.Set(Id, MakeChanged());
    REQUIRE(History.Commit());

    Edits.Forget(Id);

    CHECK(History.Undo());
    CHECK_FALSE(Edits.HasUnsavedChanges());
    CHECK(History.Redo());
    CHECK_FALSE(Edits.HasUnsavedChanges());
}

TEST_CASE_METHOD(Fixture, "Clearing the history keeps unsaved edits", "[asset edits]")
{
    Edits.Set(Id, MakeChanged());

    SECTION("Of a step")
    {
        REQUIRE(History.Commit());
        History.Clear();

        CHECK(Edits.IsDirty(Id));
        CHECK_FALSE(History.CanUndo());
    }

    SECTION("Of one that wasn't ended yet")
    {
        History.Clear();

        CHECK(Edits.IsDirty(Id));
        CHECK_FALSE(History.Commit());
    }
}

TEST_CASE_METHOD(SceneFixture, "Scene and material edits undo in the order they were made", "[asset edits]")
{
    const Entity lamp = World.CreateEntity("Lamp");
    const auto x = [&] { return lamp.GetComponent<TransformComponent>().Position.x; };

    const MaterialData original = *Edits.Get(Id);
    const MaterialData changed = MakeChanged();

    const auto move = [&](float to)
    {
        SceneUndo.Record(lamp);
        lamp.GetComponent<TransformComponent>().Position = glm::vec3(to, 0.0f, 0.0f);
        REQUIRE(History.Commit());
    };

    move(1.0f);

    Edits.Set(Id, changed);
    REQUIRE(History.Commit());

    move(2.0f);

    REQUIRE(History.Undo());
    CHECK(x() == 1.0f);
    CHECK(*Edits.Get(Id) == changed);

    REQUIRE(History.Undo());
    CHECK(x() == 1.0f);
    CHECK(*Edits.Get(Id) == original);

    REQUIRE(History.Undo());
    CHECK(x() == 0.0f);
    CHECK(*Edits.Get(Id) == original);

    REQUIRE(History.Redo());
    CHECK(x() == 1.0f);
    CHECK(*Edits.Get(Id) == original);

    REQUIRE(History.Redo());
    CHECK(x() == 1.0f);
    CHECK(*Edits.Get(Id) == changed);

    REQUIRE(History.Redo());
    CHECK(x() == 2.0f);
    CHECK(*Edits.Get(Id) == changed);
}
