#pragma once
#include <unordered_map>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/asset_manager/importers/texture_asset.hpp>

namespace crimson::editor
{
    // A separate copy of a texture for showing as it is stored, as UI that isn't sRGB encoded would show an sRGB
    // texture too dark. The caller owns it, and destroys it with GpuResourceManager::DestroyTexture.
    [[nodiscard]] TextureHandle CreateTexturePreview(GpuResourceManager& gpu, const AssetManager& assets, TextureID id);

    // The previews of texture assets, loaded the first time one is drawn. A texture goes when its asset is replaced or
    // removed, which is between frames, as nothing that draws it is recorded then. There is no ImGui id to release first.
    class AssetThumbnails
    {
    public:
        AssetThumbnails(AssetManager& assets, GpuResourceManager& gpu) : m_assets(assets), m_gpu(gpu) {}
        ~AssetThumbnails();

        AssetThumbnails(const AssetThumbnails&) = delete;
        AssetThumbnails& operator=(const AssetThumbnails&) = delete;

        // Gives the invalid handle if the texture can't be loaded
        [[nodiscard]] TextureHandle Get(TextureID id);
        void Release(AssetID id);
    private:
        AssetManager& m_assets;
        GpuResourceManager& m_gpu;
        std::unordered_map<AssetID, TextureHandle> m_thumbnails;
    };
}
