#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>

#include <crimson_editor/asset_thumbnails.hpp>

#include "support/fake_gpu.hpp"
#include "support/temp_folder.hpp"

using namespace crimson;
using crimson::editor::CreateTexturePreview;
using crimson::tests::FakeGpuResourceManager;
using crimson::tests::TempFolder;

namespace
{
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

TEST_CASE("A texture preview shows what is stored, and belongs to the caller", "[editor][assets]")
{
    const TempFolder temp;
    temp.Write("image.bmp", MakeImage());

    FakeGpuResourceManager gpu;
    AssetManager assets(gpu, temp.Path());
    const TextureID id = assets.Find<AssetType::Texture>("image.bmp");

    REQUIRE(assets.Load(id));
    CHECK(gpu.LastTextureInfo.Format == TextureFormat::RGBA8Srgb);

    const int before = gpu.LiveTextures;

    const TextureHandle preview = CreateTexturePreview(gpu, assets, id);
    REQUIRE(preview);
    CHECK(gpu.LastTextureInfo.Format == TextureFormat::RGBA8);
    CHECK(gpu.LiveTextures == before + 1);

    gpu.DestroyTexture(preview);
    CHECK(gpu.LiveTextures == before);
}
