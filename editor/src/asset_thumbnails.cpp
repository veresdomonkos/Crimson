#include "crimson_editor/asset_thumbnails.hpp"

#include <span>

#include <crimson/asset_manager/importers/texture_importer.hpp>

namespace crimson::editor
{
    TextureHandle CreateTexturePreview(GpuResourceManager& gpu, const AssetManager& assets, TextureID id)
    {
        const AssetInfo* info = assets.GetInfo(id);
        if (!info)
            return TextureHandle::Invalid();

        const std::optional<std::string> file = assets.ReadSource(id);
        if (!file)
            return TextureHandle::Invalid();

        const TextureLoadOptions options{ .GenerateMips = true, .FlipY = info->Settings.value("flipY", false) };
        return CreateTextureFromMemory(gpu, std::as_bytes(std::span(*file)), options);
    }

    AssetThumbnails::~AssetThumbnails()
    {
        for (const auto& [id, texture] : m_thumbnails)
        {
            if (texture)
                m_gpu.DestroyTexture(texture);
        }
    }

    TextureHandle AssetThumbnails::Get(TextureID id)
    {
        if (const auto it = m_thumbnails.find(id); it != m_thumbnails.end())
            return it->second;

        // A file that can't be read is remembered too, so that it isn't decoded again every frame
        const TextureHandle texture = CreateTexturePreview(m_gpu, m_assets, id);

        m_thumbnails.emplace(id, texture);
        return texture;
    }

    void AssetThumbnails::Release(AssetID id)
    {
        if (const auto it = m_thumbnails.find(id); it != m_thumbnails.end())
        {
            if (it->second)
                m_gpu.DestroyTexture(it->second);

            m_thumbnails.erase(it);
        }
    }
}
