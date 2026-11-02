#include "crimson_editor/editor_icons.hpp"

#include <crimson/asset_manager/importers/texture_importer.hpp>
#include <crimson/core/log.hpp>
#include <crimson/renderer/resource_manager.hpp>
#include <crimson/ui/imgui_backend.hpp>

#include "crimson_editor/editor_resources.hpp"

namespace crimson::editor
{
    EditorIcons::EditorIcons(GpuResourceManager& gpuResources, ImGuiBackend& imgui)
        : m_gpuResources(gpuResources), m_imgui(imgui)
    {
    }

    EditorIcons::~EditorIcons()
    {
        for (const auto& [path, icon] : m_icons)
        {
            if (!icon.Texture)
                continue;

            m_imgui.ReleaseTexture(icon.Texture);
            m_gpuResources.DestroyTexture(icon.Texture);
        }
    }

    ImTextureID EditorIcons::Get(std::string_view path)
    {
        const std::string key(path);

        if (const auto it = m_icons.find(key); it != m_icons.end())
            return it->second.Id;

        Icon icon;

        if (const EmbeddedFile* file = FindEmbeddedFile(GetEditorResources(), path))
            icon.Texture = CreateTextureFromMemory(m_gpuResources, file->Data);
        else
            LOG_ERROR("[Editor] No embedded icon at '{}'", path);

        if (icon.Texture)
            icon.Id = m_imgui.GetOrCreateTextureId(icon.Texture);

        // A missing icon is remembered too, so it is reported once
        m_icons.emplace(key, icon);
        return icon.Id;
    }
}
