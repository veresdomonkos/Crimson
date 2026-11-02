#include "crimson_editor/asset_thumbnails.hpp"

namespace crimson::editor
{
    AssetThumbnails::~AssetThumbnails()
    {
        for (const auto& [id, thumbnail] : m_thumbnails)
            Destroy(thumbnail);
    }

    ImTextureID AssetThumbnails::Get(AssetID id)
    {
        if (const auto it = m_thumbnails.find(id); it != m_thumbnails.end())
            return it->second.Id;

        // A file that can't be read is remembered too, so that it isn't decoded again every frame
        const TextureHandle texture = m_assets.CreateTexturePreview(id);
        const ImTextureID imguiId = texture ? m_imgui.GetOrCreateTextureId(texture) : ImTextureID{};

        m_thumbnails.emplace(id, Thumbnail{ texture, imguiId });
        return imguiId;
    }

    void AssetThumbnails::Release(AssetID id)
    {
        if (const auto it = m_thumbnails.find(id); it != m_thumbnails.end())
        {
            Destroy(it->second);
            m_thumbnails.erase(it);
        }
    }

    void AssetThumbnails::Destroy(const Thumbnail& thumbnail)
    {
        if (!thumbnail.Texture)
            return;

        m_imgui.ReleaseTexture(thumbnail.Texture);
        m_assets.DestroyTexturePreview(thumbnail.Texture);
    }
}
