#pragma once
#include <unordered_map>

#include <crimson/asset_manager/asset_manager.hpp>
#include <crimson/ui/imgui_backend.hpp>

namespace crimson::editor
{
    // The ImGui ids of texture assets, loaded the first time one is drawn. A texture's id stops
    // working when it is replaced or removed, so Release has to come first, between frames.
    class AssetThumbnails
    {
    public:
        AssetThumbnails(AssetManager& assets, ImGuiBackend& imgui) : m_assets(assets), m_imgui(imgui) {}
        ~AssetThumbnails();

        AssetThumbnails(const AssetThumbnails&) = delete;
        AssetThumbnails& operator=(const AssetThumbnails&) = delete;

        // Gives 0 if the texture can't be loaded
        [[nodiscard]] ImTextureID Get(AssetID id);
        void Release(AssetID id);
    private:
        struct Thumbnail
        {
            TextureHandle Texture;
            ImTextureID Id = 0;
        };

        void Destroy(const Thumbnail& thumbnail);

        AssetManager& m_assets;
        ImGuiBackend& m_imgui;
        std::unordered_map<AssetID, Thumbnail> m_thumbnails;
    };
}
