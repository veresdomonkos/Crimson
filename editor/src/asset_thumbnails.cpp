#include "crimson_editor/asset_thumbnails.hpp"

namespace crimson::editor
{
    AssetThumbnails::~AssetThumbnails()
    {
        for (const auto& [id, thumbnail] : m_thumbnails)
            m_imgui.ReleaseTexture(thumbnail.Texture);
    }

    ImTextureID AssetThumbnails::Get(AssetID id)
    {
        if (const auto it = m_thumbnails.find(id); it != m_thumbnails.end())
            return it->second.Id;

        const TextureHandle texture = m_assets.LoadTexture(id);
        if (!texture)
            return 0;

        const ImTextureID imguiId = m_imgui.GetOrCreateTextureId(texture);
        m_thumbnails.emplace(id, Thumbnail{ texture, imguiId });
        return imguiId;
    }

    void AssetThumbnails::Release(AssetID id)
    {
        if (const auto it = m_thumbnails.find(id); it != m_thumbnails.end())
        {
            m_imgui.ReleaseTexture(it->second.Texture);
            m_thumbnails.erase(it);
        }
    }
}
