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

    void OpenGLRenderer::SetShadowAtlas(TextureHandle shadowAtlas)
    {
        m_shadowAtlas = m_resourceManager.GetTexture(shadowAtlas).GLHandle;
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

    bool OpenGLRenderer::CanRender() const
    {
        return true;
    }

    bool OpenGLRenderer::Submit(FrameRecorder frame)
    {
        CRIMSON_PROFILE_FUNCTION();

        m_stats = {};

        if (!frame.IsValid())
            return false;

        const FramePacket& packet = frame.GetPacket();
        auto* window = m_device.GetPrimaryWindow();

        // ImGui and resource creation may have touched GL state since the last frame
        ResetBoundState();

        // Replacing the buffers' storage, so the frame before isn't waited on
        m_gpuData.Build(packet.GetLighting(), m_clock.Tick(std::chrono::steady_clock::now()));

        glNamedBufferSubData(m_frameUBO, 0, sizeof(FrameBlock), &m_gpuData.Block);
        UploadStorageBuffer(m_lightsBuffer, m_gpuData.Lights.data(), std::span(m_gpuData.Lights).size_bytes(), sizeof(GpuLight));
        UploadStorageBuffer(m_shadowsBuffer, m_gpuData.Shadows.data(), std::span(m_gpuData.Shadows).size_bytes(), sizeof(GpuShadow));

        const std::span<const GpuObject> objects = packet.GetObjects();
        UploadStorageBuffer(m_objectsBuffer, objects.data(), objects.size_bytes(), sizeof(GpuObject));

        BindFrameResources();

#ifndef NDEBUG
        m_validator.Check(packet, [this](RenderTargetHandle target)
        {
            const OpenGLRenderTarget& rt = m_resourceManager.GetRenderTarget(target);
            std::vector<TextureHandle> attachments = rt.ColorAttachments;
            if (rt.DepthAttachment)
                attachments.push_back(rt.DepthAttachment);
            return attachments;
        });
#endif

        const std::span<const PassRecord> passes = packet.GetPasses();
        m_stats.Passes = static_cast<uint32_t>(passes.size());

        for (const PassRecord& pass : passes)
            ExecutePass(pass);

        CRIMSON_PROFILE_PLOT("Draw Calls", m_stats.DrawCalls);

        {
            CRIMSON_PROFILE_SCOPE("Swap Buffers");
            ScopedWaitTimer waitTimer(m_stats);
            glfwSwapBuffers(window);
        }

        TracyGpuCollect;

        m_lastStats = m_stats;
        return true;
    }

    void OpenGLRenderer::ExecutePass(const PassRecord& pass)
    {
        const PassInfo& info = pass.GetInfo();

        CRIMSON_PROFILE_SCOPE_DYNAMIC(info.Name);
        TracyGpuZoneTransient(passZone, info.Name, true);

        if (pass.IsNative())
        {
            ExecuteNativePass(pass);
            return;
        }

        BeginPass(info);

        const std::span<const DrawItem> draws = pass.GetDraws();

        BuildDrawOrder(draws, m_resourceManager, m_drawOrder);
        for (const DrawSortEntry& sorted : m_drawOrder)
            ExecuteDraw(draws[sorted.Index]);
    }

    void OpenGLRenderer::BeginPass(const PassInfo& info)
    {
        ViewBlock block{};
        block.ViewProj = info.View.ViewProj;
        block.CameraPosition = glm::vec4(info.View.CameraPosition, 0.0f);
        glNamedBufferSubData(m_viewUBO, 0, sizeof(ViewBlock), &block);

        BindTarget(info);

        m_state.SetDepthTest(true);
    }

    void OpenGLRenderer::ExecuteNativePass(const PassRecord& pass)
    {
        BindTarget(pass.GetInfo());

        const NativeContext context{};
        pass.GetNativeFn()(context);

        // The callback runs code that changes GL state without going through m_state
        ResetBoundState();
    }

    void OpenGLRenderer::BindTarget(const PassInfo& info)
    {
        // A pass without a target draws to the back buffer
        const auto& target = m_resourceManager.GetRenderTarget(info.Target ? info.Target : m_resourceManager.GetBackBufferHandle());
        m_state.BindFramebuffer(target.FrameBufferHandle);

        const RenderRect rect = info.Viewport.value_or(RenderRect{ 0, 0, target.Width, target.Height });
        glViewport(rect.X, rect.Y, static_cast<GLsizei>(rect.Width), static_cast<GLsizei>(rect.Height));
        m_state.SetScissor(info.Viewport ? &rect : nullptr);

        ClearTarget(info.Clear);
    }

    void OpenGLRenderer::ClearTarget(const ClearInfo& clear)
    {
        GLbitfield clearMask = 0;
        if (HasClearFlag(clear.Flags, ClearFlags::Color))
        {
            glClearColor(clear.Color.r, clear.Color.g, clear.Color.b, clear.Color.a);
            clearMask |= GL_COLOR_BUFFER_BIT;
        }
        if (HasClearFlag(clear.Flags, ClearFlags::Depth))
        {
            // glClear obeys the depth mask, which a transparent draw may have left off
            m_state.SetDepthWrite(true);
            glClearDepth(clear.Depth);
            clearMask |= GL_DEPTH_BUFFER_BIT;
        }
        if (HasClearFlag(clear.Flags, ClearFlags::Stencil))
        {
            glClearStencil(static_cast<GLint>(clear.Stencil));
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

    void OpenGLRenderer::ExecuteDraw(const DrawItem& info)
    {
        CRIMSON_PROFILE_FUNCTION();

        OpenGLIndexBuffer& indexBuffer = m_resourceManager.GetIndexBuffer(info.IndexBuffer);
        OpenGLMaterial& material = m_resourceManager.GetMaterial(info.Material);
        const OpenGLProgram& program = m_resourceManager.GetShader(material.Shader).For(info.Variant);

        if (material.GLBufferHandle != 0 && material.IsDirty)
        {
            glNamedBufferSubData(material.GLBufferHandle, 0, static_cast<GLsizeiptr>(material.UniformDataSize), material.UniformData.get());
            material.IsDirty = false;
        }

        // Applied on every draw because clearing depth between passes can change depth write. A depth pass draws
        // everything as opaque, so a transparent mesh still casts its shadow.
        const bool transparent = info.Variant == ShaderVariant::Main && material.Settings.RenderMode == MaterialRenderMode::Transparent;

        m_state.SetBlend(transparent);
        m_state.SetDepthWrite(!transparent);
        m_state.SetCull(material.Settings.Cull);

        bool programChanged = false;

        if (info.Material != m_boundMaterial || info.Variant != m_boundVariant)
        {
            if (material.GLBufferHandle != 0)
                m_state.BindUniformBuffer(kMaterialBlockSlot, material.GLBufferHandle);

            for (const OpenGLShaderTextureUnit& unit : program.TextureUnits)
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

            programChanged = m_state.UseProgram(program.GLHandle);
            if (programChanged)
                ++m_stats.PipelineBinds;

            m_boundMaterial = info.Material;
            m_boundVariant = info.Variant;
            ++m_stats.MaterialBinds;
        }

        // Each program has its own copy of the uniform
        if (programChanged || info.Object != m_boundObject)
        {
            glUniform1ui(program.ObjectIndexLocation, info.Object);
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
