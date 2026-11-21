#include "opengl_renderer.hpp"
#include "spirv_to_glsl.hpp"
#include "utils.hpp"
#include "crimson/core/log.hpp"
#include "crimson/core/profiler.hpp"
#include "crimson/renderer/draw_state.hpp"

#include <tracy/TracyOpenGL.hpp>

namespace crimson::opengl
{
    static void SetViewport(const RenderRect& rect)
    {
        glViewport(rect.X, rect.Y, static_cast<GLsizei>(rect.Width), static_cast<GLsizei>(rect.Height));
    }

    // The scissor is only left off when the rect is the whole target
    static void SetViewportAndScissor(OpenGLStateCache& state, const RenderRect& rect, bool scissor)
    {
        SetViewport(rect);
        state.SetScissor(scissor ? &rect : nullptr);
    }

    OpenGLRenderer::OpenGLRenderer(OpenGLDevice &device, OpenGLResourceManager &resourceManager)
        :m_device(device), m_resourceManager(resourceManager)
    {
        glCreateBuffers(1, &m_frameUBO);
        glNamedBufferData(m_frameUBO, sizeof(FrameBlock), nullptr, GL_DYNAMIC_DRAW);


        glCreateBuffers(1, &m_lightsBuffer);
        glCreateBuffers(1, &m_shadowsBuffer);
        glCreateBuffers(1, &m_objectsBuffer);
        glCreateBuffers(1, &m_viewsBuffer);
        glCreateBuffers(1, &m_propertyBlocks);

        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &m_uniformOffsetAlignment);

        TracyGpuContext;
    }

    OpenGLRenderer::~OpenGLRenderer()
    {
        if (m_frameUBO != 0) { glDeleteBuffers(1, &m_frameUBO); m_frameUBO = 0; }
        if (m_propertyBlocks != 0) { glDeleteBuffers(1, &m_propertyBlocks); m_propertyBlocks = 0; }
        if (m_viewsBuffer != 0) { glDeleteBuffers(1, &m_viewsBuffer); m_viewsBuffer = 0; }
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

        // Resource creation may have touched GL state since the last frame
        ResetBoundState();

        // Replacing the buffers' storage, so the frame before isn't waited on
        m_gpuData.Build(packet.GetLighting(), packet.GetViews(), m_clock.Tick(std::chrono::steady_clock::now()));

        glNamedBufferSubData(m_frameUBO, 0, sizeof(FrameBlock), &m_gpuData.Block);
        UploadStorageBuffer(m_lightsBuffer, m_gpuData.Lights.data(), std::span(m_gpuData.Lights).size_bytes(), sizeof(GpuLight));
        UploadStorageBuffer(m_shadowsBuffer, m_gpuData.Shadows.data(), std::span(m_gpuData.Shadows).size_bytes(), sizeof(GpuShadow));

        const std::span<const GpuObject> objects = packet.GetObjects();
        UploadStorageBuffer(m_objectsBuffer, objects.data(), objects.size_bytes(), sizeof(GpuObject));
        UploadStorageBuffer(m_viewsBuffer, m_gpuData.Views.data(), std::span(m_gpuData.Views).size_bytes(), sizeof(GpuView));

        // Replacing the storage, so a frame the GPU is still reading isn't waited on. The blocks of this frame's draws with
        // properties are written into it as they are drawn.
        const auto alignment = static_cast<std::size_t>(m_uniformOffsetAlignment);
        const MaterialPropertiesBudget budget = CountMaterialProperties(packet, m_resourceManager, alignment);
        glNamedBufferData(m_propertyBlocks, static_cast<GLsizeiptr>(std::max(budget.BlockBytes, alignment)), nullptr, GL_STREAM_DRAW);
        m_propertyCursor = 0;

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
            ExecutePass(pass, packet);

        CRIMSON_PROFILE_PLOT("Draw Calls", m_stats.DrawCalls);

        {
            CRIMSON_PROFILE_SCOPE("Swap Buffers");
            ScopedWaitTimer waitTimer(m_stats);
            m_device.GetWindowGraphics().SwapBuffers();
        }

        TracyGpuCollect;

        m_lastStats = m_stats;
        return true;
    }

    void OpenGLRenderer::ExecutePass(const PassRecord& pass, const FramePacket& packet)
    {
        const PassInfo& info = pass.GetInfo();

        CRIMSON_PROFILE_SCOPE_DYNAMIC(info.Name);
        TracyGpuZoneTransient(passZone, info.Name, true);

        const RenderRect passArea = BindTarget(info);

        // BindTarget set the viewport to the pass's area, which the first draw's view may not have
        m_boundView = {};

        const std::span<const DrawItem> draws = pass.GetDraws();

        BuildDrawOrder(draws, m_resourceManager, m_drawOrder, info.Kind);
        for (const DrawSortEntry& sorted : m_drawOrder)
            ExecuteDraw(draws[sorted.Index], packet, passArea, info.Kind);
    }

    // Returns the part of the target the pass draws to
    RenderRect OpenGLRenderer::BindTarget(const PassInfo& info)
    {
        // A pass without a target draws to the back buffer
        const auto& target = m_resourceManager.GetRenderTarget(info.Target ? info.Target : m_resourceManager.GetBackBufferHandle());
        m_state.BindFramebuffer(target.FrameBufferHandle);

        const RenderRect rect = info.Viewport.value_or(RenderRect{ 0, 0, target.Width, target.Height });
        SetViewportAndScissor(m_state, rect, info.Viewport.has_value());

        ClearTarget(info.Clear);
        return rect;
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
        m_boundProperties = {};
    }

    void OpenGLRenderer::BindFrameResources()
    {
        m_state.BindUniformBuffer(kFrameBlockSlot, m_frameUBO);
        m_state.BindStorageBuffer(kLightsSlot, m_lightsBuffer);
        m_state.BindStorageBuffer(kShadowsSlot, m_shadowsBuffer);
        m_state.BindStorageBuffer(kObjectsSlot, m_objectsBuffer);
        m_state.BindStorageBuffer(kViewsSlot, m_viewsBuffer);
    }

    void OpenGLRenderer::ExecuteDraw(const DrawItem& info, const FramePacket& packet, const RenderRect& passArea, PassKind kind)
    {
        CRIMSON_PROFILE_FUNCTION();

        const std::span<const FrameView> views = packet.GetViews();

        // A view whose part of the target is outside the pass's draws nothing
        const RenderRect viewArea = ViewArea(views[info.View.Index()], passArea);
        if (IsEmpty(viewArea)) return;

        // A draw can limit itself further, to a part of what its view shows
        const RenderRect scissor = info.Scissor ? Intersect(viewArea, *info.Scissor) : viewArea;
        if (IsEmpty(scissor)) return;

        OpenGLIndexBuffer& indexBuffer = m_resourceManager.GetIndexBuffer(info.IndexBuffer);
        OpenGLMaterial& material = m_resourceManager.GetMaterial(info.Material);
        const OpenGLProgram& program = m_resourceManager.GetShader(material.Shader).For(info.Variant);

        if (material.GLBufferHandle != 0 && material.IsDirty)
        {
            glNamedBufferSubData(material.GLBufferHandle, 0, static_cast<GLsizeiptr>(material.UniformDataSize), material.UniformData.get());
            material.IsDirty = false;
        }

        // Applied on every draw because clearing depth between passes can change depth write
        const DrawState state = ResolveDrawState(kind, material.Settings, info.Variant);

        m_state.SetBlend(state.Blend);
        m_state.SetDepthTest(state.DepthTest);
        m_state.SetDepthWrite(state.DepthWrite);
        m_state.SetCull(material.Settings.Cull);

        bool programChanged = false;

        // What is bound is for the pair of the material and its properties, so the draw after one with properties binds its
        // material's own block and textures again
        if (info.Material != m_boundMaterial || info.Properties != m_boundProperties || info.Variant != m_boundVariant)
        {
            const OpenGLShader& shader = m_resourceManager.GetShader(material.Shader);
            const std::span<const MaterialPropertyEntry> entries = info.Properties ? packet.GetPropertyEntries(info.Properties) : std::span<const MaterialPropertyEntry>{};

            if (material.GLBufferHandle != 0)
            {
                if (SetsValues(entries, shader.Interface))
                {
                    const std::size_t size = material.UniformDataSize;
                    const auto alignment = static_cast<std::size_t>(m_uniformOffsetAlignment);

                    m_blockScratch.resize(size);
                    WriteMaterialBlock(m_blockScratch, { material.UniformData.get(), size }, shader.Interface, entries, packet.GetPropertyBytes(info.Properties));

                    glNamedBufferSubData(m_propertyBlocks, m_propertyCursor, static_cast<GLsizeiptr>(size), m_blockScratch.data());
                    m_state.BindUniformBufferRange(kMaterialBlockSlot, m_propertyBlocks, m_propertyCursor, static_cast<GLsizeiptr>(size));
                    m_propertyCursor += static_cast<GLintptr>((size + alignment - 1) / alignment * alignment);
                }
                else
                {
                    m_state.BindUniformBuffer(kMaterialBlockSlot, material.GLBufferHandle);
                }
            }

            for (const OpenGLShaderTextureUnit& unit : program.TextureUnits)
            {
                GLuint texture = m_shadowAtlas;

                if (!unit.IsShadowAtlas)
                {
                    const TextureHandle drawTexture = DrawTexture(unit.Texture, material.Textures, entries);
                    texture = drawTexture ? m_resourceManager.GetTexture(drawTexture).GLHandle : m_resourceManager.GetDefaultTexture();
                }

                m_state.BindTexture(unit.Unit, texture);
                m_state.BindSampler(unit.Unit, unit.Sampler);
            }

            programChanged = m_state.UseProgram(program.GLHandle);
            if (programChanged)
                ++m_stats.PipelineBinds;

            m_boundMaterial = info.Material;
            m_boundProperties = info.Properties;
            m_boundVariant = info.Variant;
            ++m_stats.MaterialBinds;
        }

        // A view draws to its own part of the target, like a shadow view to its tile of the atlas
        const bool viewChanged = info.View != m_boundView;

        if (viewChanged)
            SetViewport(viewArea);

        m_state.SetScissor(&scissor);

        // Each program has its own copy of the uniforms
        if (programChanged || info.Object != m_boundObject)
        {
            glUniform1ui(program.ObjectIndexLocation, info.Object.Index());
            m_boundObject = info.Object;
        }

        if (programChanged || viewChanged)
        {
            glUniform1ui(program.ViewIndexLocation, info.View.Index());
            m_boundView = info.View;
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

        glDrawElementsBaseVertex(GL_TRIANGLES, count, utils::GetGLIndexType(indexBuffer.Type), offset, info.BaseVertex);

        ++m_stats.DrawCalls;
        m_stats.Triangles += count / 3;
    }
}
