#pragma once
#include <string>
#include <string_view>
#include <unordered_map>

#include <crimson/renderer/resource_handles.hpp>

namespace crimson
{
    class GpuResourceManager;
}

namespace crimson::editor
{
    // The images of the editor's own UI, decoded from the executable the first time they are asked for.
    // They have nothing to do with a project's assets.
    class EditorIcons
    {
    public:
        explicit EditorIcons(GpuResourceManager& gpuResources);
        ~EditorIcons();

        EditorIcons(const EditorIcons&) = delete;
        EditorIcons& operator=(const EditorIcons&) = delete;

        // The path is relative to editor/resources. Gives the invalid handle if the image can't be used.
        [[nodiscard]] TextureHandle Get(std::string_view path);
    private:
        GpuResourceManager& m_gpuResources;
        std::unordered_map<std::string, TextureHandle> m_icons;
    };
}
