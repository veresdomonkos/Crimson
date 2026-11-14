#include <catch2/catch_test_macros.hpp>

#include <vector>

#include <crimson_editor/ui/asset_field.hpp>
#include <crimson_editor/ui/asset_picker.hpp>

#include "support/fake_gpu.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using crimson::editor::ui::AssetFieldText;
using crimson::editor::ui::AssetPicker;
using crimson::editor::ui::MovePickerSelection;
using crimson::editor::ui::PickerEntries;
using crimson::editor::ui::PickerEntry;
using crimson::tests::FakeGpuResourceManager;
using crimson::tests::TempFolder;

namespace
{
    std::vector<const AssetInfo*> Pointers(const std::vector<AssetInfo>& assets)
    {
        std::vector<const AssetInfo*> pointers;
        for (const AssetInfo& info : assets)
            pointers.push_back(&info);

        return pointers;
    }

    std::vector<std::string> NamesOf(const std::vector<PickerEntry>& entries)
    {
        std::vector<std::string> names;
        for (const PickerEntry& entry : entries)
            names.push_back(entry.Name);

        return names;
    }

    AssetInfo Material(const char* path, bool engine = false)
    {
        return AssetInfo{ .Id = AssetID::Generate(), .Type = AssetType::Material, .Path = path, .ReadOnly = engine };
    }
}

TEST_CASE("The picker lists None first, then the project's assets, then the engine's", "[asset picker]")
{
    const std::vector<AssetInfo> assets = {
        Material("materials/wood.mat"),
        Material("engine/materials/M_Default.mat", true),
        Material("Brick.mat"),
        Material("engine/materials/internal/M_Sky.mat", true),
        Material("materials/alpha.mat"),
    };

    const std::vector<const AssetInfo*> pointers = Pointers(assets);
    const std::vector<PickerEntry> entries = PickerEntries(pointers, "");

    REQUIRE(entries.size() == 5);
    CHECK_FALSE(entries[0].Id);
    CHECK(entries[0].Name == "None");

    CHECK(NamesOf(entries) == std::vector<std::string>{ "None", "alpha.mat", "Brick.mat", "wood.mat", "M_Default.mat" });

    CHECK_FALSE(entries[1].Engine);
    CHECK_FALSE(entries[3].Engine);
    CHECK(entries[4].Engine);
    CHECK(entries[4].Path == "engine/materials/M_Default.mat");
}

TEST_CASE("The picker's search matches names, ignoring case", "[asset picker]")
{
    const std::vector<AssetInfo> assets = { Material("materials/wood.mat"), Material("materials/stone.mat") };
    const std::vector<const AssetInfo*> pointers = Pointers(assets);

    CHECK(NamesOf(PickerEntries(pointers, "WOOD")) == std::vector<std::string>{ "None", "wood.mat" });

    // The folder isn't part of the name
    CHECK(NamesOf(PickerEntries(pointers, "materials")) == std::vector<std::string>{ "None" });

    CHECK(NamesOf(PickerEntries(pointers, "nothing like it")) == std::vector<std::string>{ "None" });
    CHECK(PickerEntries(pointers, "").size() == 3);
}

TEST_CASE("Arrow keys move the selection within the entries", "[asset picker]")
{
    SECTION("A list")
    {
        CHECK(MovePickerSelection(1, 4, 0, 1, 1) == 2);
        CHECK(MovePickerSelection(1, 4, 0, -1, 1) == 0);
        CHECK(MovePickerSelection(0, 4, 0, -1, 1) == 0);
        CHECK(MovePickerSelection(3, 4, 0, 1, 1) == 3);
    }

    SECTION("A grid")
    {
        CHECK(MovePickerSelection(1, 10, 1, 0, 4) == 2);
        CHECK(MovePickerSelection(1, 10, -1, 0, 4) == 0);
        CHECK(MovePickerSelection(1, 10, 0, 1, 4) == 5);
        CHECK(MovePickerSelection(5, 10, 0, -1, 4) == 1);

        // Down from the second row goes to the last entry, which is short of the third row's column
        CHECK(MovePickerSelection(7, 10, 0, 1, 4) == 9);
        CHECK(MovePickerSelection(9, 10, 1, 0, 4) == 9);
    }

    SECTION("Nothing selected goes to the first, and nothing to select stays that way")
    {
        CHECK(MovePickerSelection(-1, 5, 0, 1, 1) == 0);
        CHECK(MovePickerSelection(-1, 5, 1, 0, 4) == 0);
        CHECK(MovePickerSelection(0, 0, 0, 1, 1) == -1);
        CHECK(MovePickerSelection(-1, 0, 0, 1, 1) == -1);
    }
}

TEST_CASE("A choice reaches only the field that opened the picker, once", "[asset picker]")
{
    const AssetID chosen = AssetID::Generate();

    AssetPicker picker;
    picker.Open(1, AssetType::Texture, {}, {});
    picker.Choose(chosen);

    CHECK_FALSE(picker.TakeChoice(2));

    const std::optional<AssetID> taken = picker.TakeChoice(1);
    REQUIRE(taken);
    CHECK(*taken == chosen);

    CHECK_FALSE(picker.TakeChoice(1));
}

TEST_CASE("Confirming applies and closes, closing keeps the choice, and Reset drops it", "[asset picker]")
{
    const AssetID current = AssetID::Generate();
    const AssetID chosen = AssetID::Generate();

    AssetPicker picker;

    SECTION("Opening highlights the current asset and has nothing to apply")
    {
        picker.Open(1, AssetType::Material, current, {});

        CHECK(picker.IsOpen());
        CHECK(picker.IsOpenFor(1));
        CHECK_FALSE(picker.IsOpenFor(2));
        CHECK(picker.Selected() == current);
        CHECK_FALSE(picker.TakeChoice(1));
    }

    SECTION("Opening forgets a choice nobody took")
    {
        picker.Open(1, AssetType::Material, current, {});
        picker.Choose(chosen);
        picker.Open(1, AssetType::Material, current, {});

        CHECK_FALSE(picker.TakeChoice(1));
    }

    SECTION("Confirm applies the selection and closes")
    {
        picker.Open(1, AssetType::Material, current, {});
        picker.Choose(chosen);
        picker.Confirm();

        CHECK_FALSE(picker.IsOpen());
        CHECK(picker.TakeChoice(1) == chosen);
    }

    SECTION("Close keeps the choice")
    {
        picker.Open(1, AssetType::Material, current, {});
        picker.Choose(chosen);
        picker.Close();

        CHECK_FALSE(picker.IsOpen());
        CHECK(picker.TakeChoice(1) == chosen);
    }

    SECTION("Reset drops it")
    {
        picker.Open(1, AssetType::Material, current, {});
        picker.Choose(chosen);
        picker.Reset();

        CHECK_FALSE(picker.IsOpen());
        CHECK_FALSE(picker.TakeChoice(1));
    }

    SECTION("Cancel goes back to what the field had before the picker opened")
    {
        picker.Open(1, AssetType::Material, current, {});
        picker.Choose(chosen);
        picker.Choose(AssetID::Generate());
        picker.Cancel();

        CHECK_FALSE(picker.IsOpen());
        CHECK(picker.Selected() == current);
        CHECK(picker.TakeChoice(1) == current);
    }

    SECTION("Cancel without a choice leaves the field alone")
    {
        picker.Open(1, AssetType::Material, current, {});
        picker.Cancel();

        CHECK_FALSE(picker.IsOpen());
        CHECK_FALSE(picker.TakeChoice(1));
    }

    SECTION("Cancel after the choice was taken still goes back")
    {
        picker.Open(1, AssetType::Material, current, {});
        picker.Choose(chosen);
        REQUIRE(picker.TakeChoice(1));

        picker.Cancel();
        CHECK(picker.TakeChoice(1) == current);
    }
}

TEST_CASE("A field names its asset, None by type, or what is missing", "[asset picker]")
{
    const TempFolder temp;
    temp.Write("floor.mat", R"({ "shader": "engine/shaders/S_Lit.hlsl" })");

    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());

    const MaterialID floor = assets.Find<AssetType::Material>("floor.mat");
    REQUIRE(floor);

    CHECK(AssetFieldText(assets, floor, AssetType::Material) == "floor.mat");
    CHECK(AssetFieldText(assets, {}, AssetType::Material) == "None (Material)");
    CHECK(AssetFieldText(assets, {}, AssetType::Texture) == "None (Texture)");

    const AssetID gone = AssetID::Generate();
    assets.RememberMissing(gone, { .Path = "textures/a.png", .Type = AssetType::Texture });
    CHECK(AssetFieldText(assets, gone, AssetType::Texture) == "Missing: textures/a.png");
}
