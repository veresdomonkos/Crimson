#pragma once
#include <cstddef>
#include <vector>

#include <crimson/renderer/frame_recorder.hpp>
#include <crimson/renderer/resource_handles.hpp>
#include <crimson/renderer/resource_manager.hpp>
#include <imgui.h>

namespace crimson::editor::ui
{
    // An ImGui texture id holds the engine's texture handle, so drawing an image takes no registering and a texture can be
    // destroyed without telling ImGui first. The invalid handle is ImGui's no texture, as both are 0.
    [[nodiscard]] inline ImTextureID ToImTextureID(TextureHandle texture) { return static_cast<ImTextureID>(texture.GetRaw()); }
    [[nodiscard]] inline TextureHandle FromImTextureID(ImTextureID id) { return TextureHandle::FromRaw(id); }

    // Draws ImGui's draw data through the engine's own API: one shared material, a texture for each draw by its
    // properties, and ImGui's own textures (the fonts) kept as engine textures. It needs an ImGui context for as long as
    // it lives.
    class ImGuiRenderer
    {
    public:
        explicit ImGuiRenderer(GpuResourceManager& gpu);
        ~ImGuiRenderer();

        ImGuiRenderer(const ImGuiRenderer&) = delete;
        ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;

        // Records a pass that draws the data on top of its target. The caller says where and what it clears. This sets what
        // makes it an ImGui pass: an overlay, with the projection of the display. Everything is copied, so the draw data
        // can go before the frame is submitted.
        void Record(FrameRecorder& frame, const ImDrawData& drawData, PassInfo pass);

    private:
        void UpdateTextures(const ImDrawData& drawData);
        void UpdateBuffers(const ImDrawData& drawData);
        void DestroyTexture(ImTextureData& texture);

        GpuResourceManager& m_gpu;
        ShaderHandle m_shader;
        MaterialHandle m_material;

        VertexBufferHandle m_vertexBuffer;
        IndexBufferHandle m_indexBuffer;
        size_t m_vertexCapacity = 0; // in bytes
        size_t m_indexCapacity = 0;

        // Reused for each frame
        std::vector<ImDrawVert> m_vertices;
        std::vector<ImDrawIdx> m_indices;
        std::vector<std::byte> m_textureRows;
    };
}
