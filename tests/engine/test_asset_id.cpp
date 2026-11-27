#include <catch2/catch_test_macros.hpp>

#include <crimson/asset_manager/asset_id.hpp>
#include <crimson/asset_manager/meta_file.hpp>

#include <cctype>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "support/temp_folder.hpp"

using namespace crimson;
using crimson::tests::TempFolder;

static bool HasVersionAndVariant(const AssetID& id, int version)
{
    return (id.Bytes[6] >> 4) == version && (id.Bytes[8] >> 6) == 0b10;
}

TEST_CASE("New ids are version 4 GUIDs", "[asset id]")
{
    for (int i = 0; i < 100; ++i)
    {
        const AssetID id = AssetID::Generate();
        CHECK(id);
        CHECK(HasVersionAndVariant(id, 4));
    }
}

TEST_CASE("Ids from separate generators differ", "[asset id]")
{
    std::vector<AssetID> first, second;

    // Each thread has its own random_device
    std::thread a([&] { for (int i = 0; i < 1000; ++i) first.push_back(AssetID::Generate()); });
    std::thread b([&] { for (int i = 0; i < 1000; ++i) second.push_back(AssetID::Generate()); });
    a.join();
    b.join();

    std::unordered_set<AssetID> all(first.begin(), first.end());
    all.insert(second.begin(), second.end());
    CHECK(all.size() == 2000);
}

TEST_CASE("A name gives its RFC 9562 version 5 GUID", "[asset id]")
{
    const AssetID dns = AssetID::FromString("6ba7b810-9dad-11d1-80b4-00c04fd430c8");

    // RFC 9562, Appendix A.4
    CHECK(AssetID::FromName(dns, "www.example.com").ToString() == "2ed6657d-e927-568b-95e1-2665a8aea6a2");
}

TEST_CASE("A path gives the same id every time", "[asset id]")
{
    const AssetID cube = AssetID::FromName(kAssetPathNamespace, "engine/meshes/SM_Cube.obj");
    const AssetID dns = AssetID::FromString("6ba7b810-9dad-11d1-80b4-00c04fd430c8");

    CHECK(cube == AssetID::FromName(kAssetPathNamespace, "engine/meshes/SM_Cube.obj"));
    CHECK(cube != AssetID::FromName(kAssetPathNamespace, "engine/meshes/SM_Sphere.obj"));
    CHECK(cube != AssetID::FromName(dns, "engine/meshes/SM_Cube.obj"));
    CHECK(HasVersionAndVariant(cube, 5));
}

TEST_CASE("An id's text is the canonical GUID", "[asset id]")
{
    const AssetID id = AssetID::Generate();
    const std::string text = id.ToString();

    CHECK(std::regex_match(text, std::regex("^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")));
    CHECK(AssetID::FromString(text) == id);

    std::string upper = text;
    for (char& c : upper)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    CHECK(AssetID::FromString(upper) == id);

    CHECK(AssetID::FromString("00000000-0000-0000-0000-000000000000") == AssetID{});
}

TEST_CASE("Text that isn't a GUID is refused", "[asset id]")
{
    CHECK_THROWS_AS(AssetID::FromString("5d5ca7328e8680bb"), std::invalid_argument);
    CHECK_THROWS_AS(AssetID::FromString(""), std::invalid_argument);
    CHECK_THROWS_AS(AssetID::FromString("{6ba7b810-9dad-11d1-80b4-00c04fd430c8}"), std::invalid_argument);
    CHECK_THROWS_AS(AssetID::FromString("6ba7b8109-dad-11d1-80b4-00c04fd430c8"), std::invalid_argument);
    CHECK_THROWS_AS(AssetID::FromString("6ba7b810-9dad-11d1-80b4-00c04fd430cg"), std::invalid_argument);
}

TEST_CASE("A new id survives a save and load", "[asset id]")
{
    const TempFolder temp;
    const std::filesystem::path file = temp.Path() / "a.meta";

    AssetInfo info;
    info.Id = AssetID::Generate();
    info.Path = "a.obj";
    REQUIRE(meta::Write(file, info));

    // The importer's type is the file's type, so any will do
    const auto loaded = meta::Read(file, AssetType{});
    REQUIRE(loaded);
    CHECK(loaded->Id == info.Id);
}
