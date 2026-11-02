#pragma once
#include <string>
#include <string_view>
#include <unordered_map>

#include <crimson/renderer/resource_handles.hpp>
#include <imgui.h>

namespace crimson
{
    class GpuResourceManager;
    class ImGuiBackend;
}

namespace crimson::editor
{
    // The images of the editor's own UI, decoded from the executable the first time they are asked for.
    // They have nothing to do with a project's assets.
    class EditorIcons
    {
    public:
        EditorIcons(GpuResourceManager& gpuResources, ImGuiBackend& imgui);
        ~EditorIcons();

        EditorIcons(const EditorIcons&) = delete;
        EditorIcons& operator=(const EditorIcons&) = delete;

        // The path is relative to editor/resources. Gives 0 if the image can't be used.
        [[nodiscard]] ImTextureID Get(std::string_view path);
    private:
        struct Icon
        {
            TextureHandle Texture;
            ImTextureID Id = 0;
        };

        GpuResourceManager& m_gpuResources;
        ImGuiBackend& m_imgui;
        std::unordered_map<std::string, Icon> m_icons;
    };
}
