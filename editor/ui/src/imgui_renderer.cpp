#include "imgui_renderer.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>

#include <glm/gtc/matrix_transform.hpp>

#include <crimson/core/embedded_resource.hpp>
#include <crimson/core/log.hpp>
#include <crimson/renderer/shader_compiler.hpp>

#include "imgui_convert.hpp"
#include "ui_resources.hpp"

namespace crimson::editor::ui
{
    // The vertex and the index are what ImGui writes, so the buffers are filled with them as they are
    static_assert(sizeof(ImDrawVert) == 20);
    static_assert(sizeof(ImDrawIdx) == 2);

    // The texture of S_ImGui.hlsl, which each draw sets by its properties
    static const PropertyId kTextureProperty("Texture");

    // A buffer that has to grow gets room to spare, so it doesn't grow again next frame
    constexpr float kBufferGrowth = 1.5f;

    constexpr const char* kShaderPath = "shaders/S_ImGui.hlsl";

    // ImGui asks the renderer to reset its state with this callback, and the state here is the same for every draw, so
    // there is nothing to reset. ImGui needs it set, as it leaves the slot empty.
    static void ResetRenderState(const ImDrawList*, const ImDrawCmd*) {}

    ImGuiRenderer::ImGuiRenderer(GpuResourceManager& gpu)
        : m_gpu(gpu)
    {
        const EmbeddedFile* file = FindEmbeddedFile(GetUiResources(), kShaderPath);
        if (!file)
        {
            LOG_ERROR("[Editor] No embedded shader at '{}'", kShaderPath);
            throw std::runtime_error("The ImGui shader is missing");
        }

        const auto binary = shader_compiler::Compile("S_ImGui.hlsl", std::string(file->Text()), {});
        if (!binary)
            throw std::runtime_error("The ImGui shader doesn't compile");

        m_shader = m_gpu.CreateShader(*binary);
        if (!m_shader)
            throw std::runtime_error("The ImGui shader can't be created");

        m_material = m_gpu.CreateMaterial(m_shader, MaterialSettings{ .Cull = CullMode::None });
        if (!m_material)
        {
            m_gpu.DestroyShader(m_shader);
            throw std::runtime_error("The ImGui material can't be created");
        }

        ImGuiIO& io = ImGui::GetIO();
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendRendererName = "crimson";
        ImGui::GetPlatformIO().DrawCallback_ResetRenderState = ResetRenderState;
    }

    ImGuiRenderer::~ImGuiRenderer()
    {
        for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
        {
            if (texture->GetTexID() != ImTextureID_Invalid)
                DestroyTexture(*texture);
        }

        if (m_vertexBuffer)
            m_gpu.DestroyVertexBuffer(m_vertexBuffer);

        if (m_indexBuffer)
            m_gpu.DestroyIndexBuffer(m_indexBuffer);

        m_gpu.DestroyMaterial(m_material);
        m_gpu.DestroyShader(m_shader);

        ImGuiIO& io = ImGui::GetIO();
        io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset);
        io.BackendRendererName = nullptr;
        ImGui::GetPlatformIO().DrawCallback_ResetRenderState = nullptr;
    }

    void ImGuiRenderer::DestroyTexture(ImTextureData& texture)
    {
        m_gpu.DestroyTexture(FromImTextureID(texture.GetTexID()));
        texture.SetTexID(ImTextureID_Invalid);
        texture.SetStatus(ImTextureStatus_Destroyed);
    }

    void ImGuiRenderer::UpdateTextures(const ImDrawData& drawData)
    {
        if (!drawData.Textures)
            return;

        for (ImTextureData* texture : *drawData.Textures)
        {
            assert(texture->Format == ImTextureFormat_RGBA32 && "The renderer only makes 4 byte textures");

            if (texture->Status == ImTextureStatus_WantCreate)
            {
                const TextureHandle handle = m_gpu.CreateTexture(
                    { .Width = static_cast<uint32_t>(texture->Width), .Height = static_cast<uint32_t>(texture->Height), .Format = TextureFormat::RGBA8 },
                    texture->GetPixels());

                if (!handle)
                {
                    LOG_ERROR("[Editor] An ImGui texture of {}x{} couldn't be created", texture->Width, texture->Height);
                    continue;
                }

                texture->SetTexID(ToImTextureID(handle));
                texture->SetStatus(ImTextureStatus_OK);
            }
            else if (texture->Status == ImTextureStatus_WantUpdates)
            {
                const TextureHandle handle = FromImTextureID(texture->GetTexID());

                for (const ImTextureRect& rect : texture->Updates)
                {
                    // The rows of the rect are apart in ImGui's pixels, and the engine takes them packed
                    const size_t rowBytes = static_cast<size_t>(rect.w) * texture->BytesPerPixel;
                    m_textureRows.resize(rowBytes * rect.h);

                    for (int row = 0; row < rect.h; ++row)
                        std::memcpy(m_textureRows.data() + row * rowBytes, texture->GetPixelsAt(rect.x, rect.y + row), rowBytes);

                    m_gpu.UpdateTexture(handle, { .X = rect.x, .Y = rect.y, .Width = rect.w, .Height = rect.h }, m_textureRows.data());
                }

                texture->SetStatus(ImTextureStatus_OK);
            }
            else if (texture->Status == ImTextureStatus_WantDestroy && texture->UnusedFrames >= static_cast<int>(kMaxFramesInFlight))
            {
                // Frames still in flight may be drawing it, until it has been unused for as many
                DestroyTexture(*texture);
            }
        }
    }

    void ImGuiRenderer::UpdateBuffers(const ImDrawData& drawData)
    {
        const size_t vertexBytes = static_cast<size_t>(drawData.TotalVtxCount) * sizeof(ImDrawVert);
        const size_t indexBytes = static_cast<size_t>(drawData.TotalIdxCount) * sizeof(ImDrawIdx);

        if (vertexBytes > m_vertexCapacity)
        {
            if (m_vertexBuffer)
                m_gpu.DestroyVertexBuffer(m_vertexBuffer);

            m_vertexCapacity = static_cast<size_t>(static_cast<float>(vertexBytes) * kBufferGrowth);
            m_vertexBuffer = m_gpu.CreateVertexBuffer(
                { .Layout = { ShaderDataType::Float2, ShaderDataType::Float2, ShaderDataType::UByte4Norm }, .Size = m_vertexCapacity, .Usage = BufferUsage::Stream },
                nullptr);
        }

        if (indexBytes > m_indexCapacity)
        {
            if (m_indexBuffer)
                m_gpu.DestroyIndexBuffer(m_indexBuffer);

            m_indexCapacity = static_cast<size_t>(static_cast<float>(indexBytes) * kBufferGrowth);
            m_indexBuffer = m_gpu.CreateIndexBuffer({ .Size = m_indexCapacity, .Usage = BufferUsage::Stream, .Type = IndexType::UInt16 }, nullptr);
        }

        m_vertices.clear();
        m_indices.clear();

        for (const ImDrawList* list : drawData.CmdLists)
        {
            m_vertices.insert(m_vertices.end(), list->VtxBuffer.begin(), list->VtxBuffer.end());
            m_indices.insert(m_indices.end(), list->IdxBuffer.begin(), list->IdxBuffer.end());
        }

        m_gpu.UpdateVertexBuffer(m_vertexBuffer, std::as_bytes(std::span(m_vertices)));
        m_gpu.UpdateIndexBuffer(m_indexBuffer, std::as_bytes(std::span(m_indices)));
    }

    void ImGuiRenderer::Record(FrameRecorder& frame, const ImDrawData& drawData, PassInfo pass)
    {
        const float width = drawData.DisplaySize.x * drawData.FramebufferScale.x;
        const float height = drawData.DisplaySize.y * drawData.FramebufferScale.y;

        if (width <= 0.0f || height <= 0.0f)
            return;

        UpdateTextures(drawData);

        if (drawData.TotalVtxCount > 0 && drawData.TotalIdxCount > 0)
            UpdateBuffers(drawData);

        // The top of the display is the top of clip space, and the display's own corner is where it starts
        pass.Kind = PassKind::Overlay;
        pass.View.ViewProj = glm::orthoRH_ZO(
            drawData.DisplayPos.x, drawData.DisplayPos.x + drawData.DisplaySize.x,
            drawData.DisplayPos.y + drawData.DisplaySize.y, drawData.DisplayPos.y, 0.0f, 1.0f);

        PassBuilder builder = frame.AddPass(pass);

        const ImDrawCallback resetRenderState = ResetRenderState;

        uint32_t vertexOffset = 0;
        uint32_t indexOffset = 0;

        // Neighbouring draws with a texture share its properties
        TextureHandle texture;
        MaterialPropertiesId properties;

        for (const ImDrawList* list : drawData.CmdLists)
        {
            for (const ImDrawCmd& cmd : list->CmdBuffer)
            {
                if (cmd.UserCallback)
                {
                    // Nothing is kept between draws that needs resetting
                    if (cmd.UserCallback != resetRenderState)
                        cmd.UserCallback(list, &cmd);

                    continue;
                }

                // The clip rect in the display's pixels from the top, which the scissor takes as rows from the bottom
                const int left = std::max(0, static_cast<int>(std::floor((cmd.ClipRect.x - drawData.DisplayPos.x) * drawData.FramebufferScale.x)));
                const int top = std::max(0, static_cast<int>(std::floor((cmd.ClipRect.y - drawData.DisplayPos.y) * drawData.FramebufferScale.y)));
                const int right = std::min(static_cast<int>(width), static_cast<int>(std::ceil((cmd.ClipRect.z - drawData.DisplayPos.x) * drawData.FramebufferScale.x)));
                const int bottom = std::min(static_cast<int>(height), static_cast<int>(std::ceil((cmd.ClipRect.w - drawData.DisplayPos.y) * drawData.FramebufferScale.y)));

                if (right <= left || bottom <= top)
                    continue;

                const TextureHandle cmdTexture = FromImTextureID(cmd.GetTexID());
                if (!properties || cmdTexture != texture)
                {
                    texture = cmdTexture;
                    properties = frame.AddProperties(MaterialProperties().SetTexture(kTextureProperty, texture));
                }

                builder.Draw(DrawItem{
                    .VertexBuffer = m_vertexBuffer,
                    .IndexBuffer = m_indexBuffer,
                    .Material = m_material,
                    .Properties = properties,
                    .FirstIndex = indexOffset + cmd.IdxOffset,
                    .IndexCount = cmd.ElemCount,
                    .Scissor = RenderRect{ left, static_cast<int>(height) - bottom, static_cast<uint32_t>(right - left), static_cast<uint32_t>(bottom - top) },
                    .BaseVertex = static_cast<int32_t>(vertexOffset + cmd.VtxOffset)
                });
            }

            vertexOffset += static_cast<uint32_t>(list->VtxBuffer.Size);
            indexOffset += static_cast<uint32_t>(list->IdxBuffer.Size);
        }
    }
}
