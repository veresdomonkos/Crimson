#include <stb_image/stb_image.h>

#include "crimson/asset_manager/importers/texture_importer.hpp"

#include <algorithm>
#include <bit>

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
        return { { "srgb", true }, { "generateMips", true }, { "flipY", false } };
    }

    TextureHandle CreateTextureFromMemory(GpuResourceManager& gpu, std::span<const std::byte> file, const TextureLoadOptions& options)
    {
        int w = 0, h = 0, channels = 0;

        stbi_set_flip_vertically_on_load_thread(options.FlipY ? 1 : 0);
        stbi_uc* pixels = stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(file.data()), static_cast<int>(file.size()), &w, &h, &channels, STBI_rgb_alpha);

        if (!pixels)
        {
            LOG_ERROR("[Assets] Failed to decode image: {}", stbi_failure_reason());
            return TextureHandle::Invalid();
        }

        TextureInfo info{};
        info.Width     = static_cast<uint32_t>(w);
        info.Height    = static_cast<uint32_t>(h);
        info.Format    = TextureFormat::RGBA8;
        info.Usage     = TextureUsage::Sampled;
        info.MipLevels = options.GenerateMips ? static_cast<uint32_t>(std::bit_width(static_cast<uint32_t>(std::max(w, h)))) : 1;

        TextureHandle handle = gpu.CreateTexture(info, pixels);
        stbi_image_free(pixels);

        return handle;
    }

    std::optional<std::pair<int, int>> ReadImageSize(std::span<const std::byte> file)
    {
        int width = 0, height = 0, channels = 0;

        if (!stbi_info_from_memory(reinterpret_cast<const stbi_uc*>(file.data()), static_cast<int>(file.size()), &width, &height, &channels))
            return std::nullopt;

        return std::pair{ width, height };
    }

    LoadedAsset TextureImporter::Load(const AssetSource& source, ImportContext& ctx)
    {
        const auto file = source.Read();
        if (!file)
            return {};

        const TextureLoadOptions options{
            .GenerateMips = source.Info.Settings.value("generateMips", false),
            .FlipY = source.Info.Settings.value("flipY", false)
        };

        const TextureHandle handle = CreateTextureFromMemory(ctx.Gpu, std::as_bytes(std::span(*file)), options);
        if (!handle)
            return {};

        return handle;
    }

    void TextureImporter::Unload(const LoadedAsset& asset, GpuResourceManager& gpu)
    {
        if (const auto* tex = std::get_if<TextureHandle>(&asset))
            gpu.DestroyTexture(*tex);
    }
}