#include <stb_image/stb_image.h>

#include "crimson/asset_manager/importers/texture_importer.hpp"
#include "crimson/core/log.hpp"

namespace crimson
{
    std::span<const std::string_view> TextureImporter::Extensions() const
    {
        static constexpr std::string_view exts[] = { ".png", ".jpg", ".jpeg", ".tga", ".bmp" };
        return exts;
    }

    nlohmann::json TextureImporter::DefaultSettings() const
    {
        return { { "srgb", true }, { "generateMips", false } };
    }

    LoadedAsset TextureImporter::Load(const AssetMetadata& meta, const std::filesystem::path& path, ImportContext& ctx)
    {
        int w = 0, h = 0, channels = 0;
        stbi_uc* pixels = stbi_load(path.string().c_str(), &w, &h, &channels, STBI_rgb_alpha);

        if (!pixels)
        {
            LOG_ERROR("[Assets] Failed to load {}: {}", path.string(), stbi_failure_reason());
            return {};
        }

        TextureInfo info{};
        info.Width     = static_cast<uint32_t>(w);
        info.Height    = static_cast<uint32_t>(h);
        info.Format    = TextureFormat::RGBA8;
        info.Usage     = TextureUsage::Sampled;
        info.MipLevels = 1;

        TextureHandle handle = ctx.Gpu.CreateTexture(info, pixels);
        stbi_image_free(pixels);

        if (!handle) return {};
        return handle;
    }

    void TextureImporter::Unload(const LoadedAsset& asset, GpuResourceManager& gpu)
    {
        if (const auto* tex = std::get_if<TextureHandle>(&asset))
            gpu.DestroyTexture(*tex);
    }
}