#include "opengl_renderer.hpp"
#include "spirv_to_glsl.hpp"
#include "utils.hpp"
#include "crimson/core/log.hpp"
#include "crimson/core/profiler.hpp"

#include <tracy/TracyOpenGL.hpp>

namespace crimson::opengl
{
    OpenGLRenderer::OpenGLRenderer(OpenGLDevice &device, OpenGLResourceManager &resourceManager)
        :m_device(device), m_resourceManager(resourceManager)
    {
        glCreateBuffers(1, &m_frameUBO);
        glNamedBufferData(m_frameUBO, sizeof(FrameBlock), nullptr, GL_DYNAMIC_DRAW);

        glCreateBuffers(1, &m_viewUBO);
        glNamedBufferData(m_viewUBO, sizeof(ViewBlock), nullptr, GL_DYNAMIC_DRAW);

        glCreateBuffers(1, &m_lightsBuffer);
        glCreateBuffers(1, &m_shadowsBuffer);
        glCreateBuffers(1, &m_objectsBuffer);

        TracyGpuContext;
    }

    OpenGLRenderer::~OpenGLRenderer()
    {
        if (m_frameUBO != 0) { glDeleteBuffers(1, &m_frameUBO); m_frameUBO = 0; }
        if (m_viewUBO != 0) { glDeleteBuffers(1, &m_viewUBO); m_viewUBO = 0; }
        if (m_lightsBuffer != 0) { glDeleteBuffers(1, &m_lightsBuffer); m_lightsBuffer = 0; }
        if (m_shadowsBuffer != 0) { glDeleteBuffers(1, &m_shadowsBuffer); m_shadowsBuffer = 0; }
        if (m_objectsBuffer != 0) { glDeleteBuffers(1, &m_objectsBuffer); m_objectsBuffer = 0; }
    }

    void OpenGLRenderer::SetShadowMap(TextureHandle shadowMap)
    {
        m_shadowAtlas = m_resourceManager.GetTexture(shadowMap).GLHandle;
    }

    // Replaces the buffer's storage, so a frame the GPU is still reading isn't waited on.
    // An empty buffer can't be bound, so it stays at least one element large.
    static void UploadStorageBuffer(GLuint buffer, const void* data, std::size_t size, std::size_t minimumSize)
    {
        if (size == 0)
            glNamedBufferData(buffer, static_cast<GLsizeiptr>(minimumSize), nullptr, GL_STREAM_DRAW);
        else
            glNamedBufferData(buffer, static_cast<GLsizeiptr>(size), data, GL_STREAM_DRAW);
    }

    FrameContext OpenGLRenderer::BeginFrame(const FrameLightingData& lighting)
    {
        CRIMSON_PROFILE_FUNCTION();

        m_stats = {};

        m_gpuData.Build(lighting);

        glNamedBufferSubData(m_frameUBO, 0, sizeof(FrameBlock), &m_gpuData.Block);
        UploadStorageBuffer(m_lightsBuffer, m_gpuData.Lights.data(), std::span(m_gpuData.Lights).size_bytes(), sizeof(GpuLight));
        UploadStorageBuffer(m_shadowsBuffer, m_gpuData.Shadows.data(), std::span(m_gpuData.Shadows).size_bytes(), sizeof(GpuShadow));

        m_frames[0].Reset();
        m_frames[0].Init(m_resourceManager.GetBackBufferHandle(), true);
        return m_frames[0].CreateContext();
    }

    void OpenGLRenderer::EndFrame(const FrameContext& frameContext)
    {
        CRIMSON_PROFILE_FUNCTION();

        Frame& frame = m_frames[frameContext.GetIndex()];
        auto* window = m_device.GetPrimaryWindow();

        // ImGui and resource creation may have touched GL state since the last frame
        ResetBoundState();

        const std::span<const ObjectData> objects = frame.GetObjects();
        UploadStorageBuffer(m_objectsBuffer, objects.data(), objects.size_bytes(), sizeof(ObjectData));

        BindFrameResources();

        m_stats.Passes = static_cast<uint32_t>(frame.GetPasses().size());

        for (const auto& entry : frame.GetPasses())
        {
            if (const auto* materialPass = std::get_if<RenderPass>(&entry))
            {
                CRIMSON_PROFILE_SCOPE("Render Pass");
                TracyGpuZone("Render Pass");

                ExecuteBeginRenderPass(materialPass->Info());

                const std::span<const DrawInfo> draws = materialPass->GetDraws();

                BuildDrawOrder(draws, m_resourceManager, m_drawOrder);
                for (const DrawSortEntry& sorted : m_drawOrder)
                    ExecuteDraw(draws[sorted.Index]);
            }
            else if (const auto* rawPass = std::get_if<RawPass>(&entry))
            {
                CRIMSON_PROFILE_SCOPE("Raw Pass");
                TracyGpuZone("Raw Pass");

                ExecuteBeginRawPass(rawPass->Info());
            }
        }

        CRIMSON_PROFILE_PLOT("Draw Calls", m_stats.DrawCalls);

        {
            CRIMSON_PROFILE_SCOPE("Swap Buffers");
            ScopedWaitTimer waitTimer(m_stats);
            glfwSwapBuffers(window);
        }

        TracyGpuCollect;

        m_lastStats = m_stats;
    }

    void OpenGLRenderer::ExecuteBeginRenderPass(const RenderPassInfo& info)
    {
        ViewBlock block{};
        block.ViewProj = info.ViewProj;
        block.CameraPosition = glm::vec4(info.CameraPosition, 0.0f);
        glNamedBufferSubData(m_viewUBO, 0, sizeof(ViewBlock), &block);

        const auto& target = m_resourceManager.GetRenderTarget(info.Target);
        m_state.BindFramebuffer(target.FrameBufferHandle);

        const RenderRect rect = info.Viewport.value_or(RenderRect{ 0, 0, target.Width, target.Height });
        glViewport(rect.X, rect.Y, static_cast<GLsizei>(rect.Width), static_cast<GLsizei>(rect.Height));
        m_state.SetScissor(info.Viewport ? &rect : nullptr);

        ClearTarget(info.ClearFlags, info.ClearColor, info.ClearDepth, info.ClearStencil);

        m_state.SetDepthTest(true);
    }

    void OpenGLRenderer::ExecuteBeginRawPass(const RawPassInfo &info)
    {
        const auto& target = m_resourceManager.GetRenderTarget(info.Target);
        m_state.BindFramebuffer(target.FrameBufferHandle);
        glViewport(0, 0, static_cast<GLsizei>(target.Width), static_cast<GLsizei>(target.Height));
        m_state.SetScissor(nullptr);

        ClearTarget(info.ClearFlags, info.ClearColor, info.ClearDepth, info.ClearStencil);

        NativeFrameHandles handles{};
        info.Callback(handles);

        // The callback runs code that changes GL state without going through m_state
        ResetBoundState();
    }

    void OpenGLRenderer::ClearTarget(ClearFlags flags, const glm::vec4& color, float depth, uint32_t stencil)
    {
        GLbitfield clearMask = 0;
        if (HasClearFlag(flags, ClearFlags::Color))
        {
            glClearColor(color.r, color.g, color.b, color.a);
            clearMask |= GL_COLOR_BUFFER_BIT;
        }
        if (HasClearFlag(flags, ClearFlags::Depth))
        {
            // glClear obeys the depth mask, which a transparent draw may have left off
            m_state.SetDepthWrite(true);
            glClearDepth(depth);
            clearMask |= GL_DEPTH_BUFFER_BIT;
        }
        if (HasClearFlag(flags, ClearFlags::Stencil))
        {
            glClearStencil(static_cast<GLint>(stencil));
            clearMask |= GL_STENCIL_BUFFER_BIT;
        }
        if (clearMask != 0) glClear(clearMask);
    }

    void OpenGLRenderer::ResetBoundState()
    {
        m_state.Reset();
        m_boundMaterial = MaterialHandle::Invalid();
        m_boundVertexBuffer = VertexBufferHandle::Invalid();
        m_boundIndexBuffer = IndexBufferHandle::Invalid();
    }

    void OpenGLRenderer::BindFrameResources()
    {
        m_state.BindUniformBuffer(kFrameBlockSlot, m_frameUBO);
        m_state.BindUniformBuffer(kViewBlockSlot, m_viewUBO);
        m_state.BindStorageBuffer(kLightsSlot, m_lightsBuffer);
        m_state.BindStorageBuffer(kShadowsSlot, m_shadowsBuffer);
        m_state.BindStorageBuffer(kObjectsSlot, m_objectsBuffer);
    }

    void OpenGLRenderer::ExecuteDraw(const DrawInfo& info)
    {
        CRIMSON_PROFILE_FUNCTION();

        OpenGLIndexBuffer& indexBuffer = m_resourceManager.GetIndexBuffer(info.IndexBuffer);
        OpenGLMaterial& material = m_resourceManager.GetMaterial(info.Material);
        OpenGLShader& shader = m_resourceManager.GetShader(material.Shader);

        if (material.GLBufferHandle != 0 && material.IsDirty)
        {
            glNamedBufferSubData(material.GLBufferHandle, 0, static_cast<GLsizeiptr>(material.UniformDataSize), material.UniformData.get());
            material.IsDirty = false;
        }

        // Applied on every draw because clearing depth between passes can change depth write
        const bool transparent = material.Settings.RenderMode == MaterialRenderMode::Transparent;

        m_state.SetBlend(transparent);
        m_state.SetDepthWrite(!transparent);
        m_state.SetCull(material.Settings.Cull);

        bool programChanged = false;

        if (info.Material != m_boundMaterial)
        {
            if (material.GLBufferHandle != 0)
                m_state.BindUniformBuffer(kMaterialBlockSlot, material.GLBufferHandle);

            for (const OpenGLShaderTextureUnit& unit : shader.TextureUnits)
            {
                GLuint texture = m_shadowAtlas;

                if (!unit.IsShadowAtlas)
                {
                    const auto it = material.Textures.find(unit.Texture);
                    texture = it != material.Textures.end() ? it->second : m_resourceManager.GetDefaultTexture();
                }

                m_state.BindTexture(unit.Unit, texture);
                m_state.BindSampler(unit.Unit, unit.Sampler);
            }

            programChanged = m_state.UseProgram(shader.GLHandle);
            if (programChanged)
                ++m_stats.PipelineBinds;

            m_boundMaterial = info.Material;
            ++m_stats.MaterialBinds;
        }

        // Each program has its own copy of the uniform
        if (programChanged || info.Object != m_boundObject)
        {
            glUniform1ui(shader.ObjectIndexLocation, info.Object);
            m_boundObject = info.Object;
        }

        if (info.VertexBuffer != m_boundVertexBuffer || info.IndexBuffer != m_boundIndexBuffer)
        {
            if (m_state.BindVertexArray(m_resourceManager.GetOrCreateVertexArray(info.VertexBuffer, info.IndexBuffer)))
                ++m_stats.BufferBinds;

            m_boundVertexBuffer = info.VertexBuffer;
            m_boundIndexBuffer = info.IndexBuffer;
        }

        const GLsizei total = static_cast<GLsizei>(indexBuffer.Size / Index::Size(indexBuffer.Type));
        const GLsizei count = info.IndexCount ? info.IndexCount : total;
        const auto offset = reinterpret_cast<const void*>(static_cast<uintptr_t>(info.FirstIndex) * Index::Size(indexBuffer.Type));

        glDrawElements(GL_TRIANGLES, count, utils::GetGLIndexType(indexBuffer.Type), offset);

        ++m_stats.DrawCalls;
        m_stats.Triangles += count / 3;
    }
}
