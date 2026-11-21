#include "crimson_editor/editor_icons.hpp"

#include <crimson/asset_manager/importers/texture_importer.hpp>
#include <crimson/core/log.hpp>
#include <crimson/renderer/resource_manager.hpp>

#include "crimson_editor/editor_resources.hpp"

namespace crimson::editor
{
    EditorIcons::EditorIcons(GpuResourceManager& gpuResources)
        : m_gpuResources(gpuResources)
    {
    }

    EditorIcons::~EditorIcons()
    {
        for (const auto& [path, texture] : m_icons)
        {
            if (texture)
                m_gpuResources.DestroyTexture(texture);
        }
    }

    TextureHandle EditorIcons::Get(std::string_view path)
    {
        const std::string key(path);

        if (const auto it = m_icons.find(key); it != m_icons.end())
            return it->second;

        TextureHandle texture;

        if (const EmbeddedFile* file = FindEmbeddedFile(GetEditorResources(), path))
            texture = CreateTextureFromMemory(m_gpuResources, file->Data);
        else
            LOG_ERROR("[Editor] No embedded icon at '{}'", path);

        // A missing icon is remembered too, so it is reported once
        m_icons.emplace(key, texture);
        return texture;
    }
}
