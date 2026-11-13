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
        for (const auto& [id, thumbnail] : m_thumbnails)
            Destroy(thumbnail);
    }

    ImTextureID AssetThumbnails::Get(TextureID id)
    {
        if (const auto it = m_thumbnails.find(id); it != m_thumbnails.end())
            return it->second.Id;

        // A file that can't be read is remembered too, so that it isn't decoded again every frame
        const TextureHandle texture = CreateTexturePreview(m_gpu, m_assets, id);
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
        m_gpu.DestroyTexture(thumbnail.Texture);
    }
}
