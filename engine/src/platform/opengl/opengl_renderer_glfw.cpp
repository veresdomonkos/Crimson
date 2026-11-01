#include "opengl_renderer.hpp"
#include "utils.hpp"
#include "crimson/core/log.hpp"
#include "crimson/core/profiler.hpp"

#include <tracy/TracyOpenGL.hpp>

namespace crimson::opengl
{
    OpenGLRenderer::OpenGLRenderer(OpenGLDevice &device, OpenGLResourceManager &resourceManager)
        :m_device(device), m_resourceManager(resourceManager)
    {
        glCreateBuffers(1, &m_cameraUBO);
        glNamedBufferData(m_cameraUBO, sizeof(CameraBlock), nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, kCameraBlockBinding, m_cameraUBO);

        glCreateBuffers(1, &m_lightingUBO);
        glNamedBufferData(m_lightingUBO, sizeof(LightingBlock), nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, kLightingBlockBinding, m_lightingUBO);

        TracyGpuContext;
    }

    OpenGLRenderer::~OpenGLRenderer()
    {
        if (m_cameraUBO != 0) { glDeleteBuffers(1, &m_cameraUBO); m_cameraUBO = 0; }
        if (m_lightingUBO != 0) { glDeleteBuffers(1, &m_lightingUBO); m_lightingUBO = 0; }
    }

    void OpenGLRenderer::SetShadowMap(TextureHandle shadowMap)
    {
        const OpenGLTexture& tex = m_resourceManager.GetTexture(shadowMap);
        glBindTextureUnit(kShadowMapBinding, tex.GLHandle);
    }

    FrameContext OpenGLRenderer::BeginFrame(const FrameLightingData& lighting)
    {
        CRIMSON_PROFILE_FUNCTION();

        m_stats = {};

        LightingBlock block{};
        block.AmbientColor = glm::vec4(lighting.AmbientColor, 0.0f);
        block.ShadowViewProj = lighting.ShadowViewProj;
        block.ShadowLightIndex = lighting.ShadowLightIndex;
        block.LightCount = std::min<uint32_t>(static_cast<uint32_t>(lighting.Lights.size()), kMaxLights);

        for (uint32_t i = 0; i < block.LightCount; ++i)
            block.Lights[i] = lighting.Lights[i].ToGPULight();

        glNamedBufferSubData(m_lightingUBO, 0, sizeof(LightingBlock), &block);

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

        m_stats.Passes = static_cast<uint32_t>(frame.GetPasses().size());

        for (const auto& entry : frame.GetPasses())
        {
            if (const auto* materialPass = std::get_if<RenderPass>(&entry))
            {
                CRIMSON_PROFILE_SCOPE("Render Pass");
                TracyGpuZone("Render Pass");

                ExecuteBeginRenderPass(materialPass->Info());

                m_resourceManager.BuildDrawOrder(materialPass->GetDraws(), m_drawOrder);
                for (const SortedDraw& sorted : m_drawOrder)
                    ExecuteDraw(*sorted.Draw);
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
        CameraBlock block{};
        block.ViewProj = info.ViewProj;
        block.Position = glm::vec4(info.CameraPosition, 0.0f);
        glNamedBufferSubData(m_cameraUBO, 0, sizeof(CameraBlock), &block);

        const auto& target = m_resourceManager.GetRenderTarget(info.Target);
        m_state.BindFramebuffer(target.FrameBufferHandle);
        glViewport(0, 0, static_cast<GLsizei>(target.Width), static_cast<GLsizei>(target.Height));

        ClearTarget(info.ClearFlags, info.ClearColor, info.ClearDepth, info.ClearStencil);

        m_state.SetDepthTest(true);
    }

    void OpenGLRenderer::ExecuteBeginRawPass(const RawPassInfo &info)
    {
        const auto& target = m_resourceManager.GetRenderTarget(info.Target);
        m_state.BindFramebuffer(target.FrameBufferHandle);
        glViewport(0, 0, static_cast<GLsizei>(target.Width), static_cast<GLsizei>(target.Height));

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

    void OpenGLRenderer::ExecuteDraw(const DrawInfo& info)
    {
        CRIMSON_PROFILE_FUNCTION();

        OpenGLIndexBuffer& indexBuffer = m_resourceManager.GetIndexBuffer(info.IndexBuffer);
        OpenGLMaterial& material = m_resourceManager.GetMaterial(info.Material);
        OpenGLShader& shader = m_resourceManager.GetShader(material.Shader);

        if (material.GLBufferHandle != 0 && shader.UBOSize > 0)
        {
            if (material.IsDirty)
            {
                glNamedBufferSubData(material.GLBufferHandle, 0, static_cast<GLsizeiptr>(material.UniformDataSize), material.UniformData.get());
                material.IsDirty = false;
            }
        }

        // Applied on every draw because clearing depth between passes can change depth write
        const bool transparent = material.Settings.RenderMode == MaterialRenderMode::Transparent;

        m_state.SetBlend(transparent);
        m_state.SetDepthWrite(!transparent);
        m_state.SetCull(material.Settings.Cull);

        if (info.Material != m_boundMaterial)
        {
            if (material.GLBufferHandle != 0 && shader.UBOSize > 0)
                m_state.BindUniformBuffer(shader.MaterialUboBinding, material.GLBufferHandle);

            for (const auto& [bindingUnit, glTexture] : material.BoundTextures)
                m_state.BindTexture(bindingUnit, glTexture);

            if (m_state.UseProgram(shader.GLHandle))
                ++m_stats.PipelineBinds;

            m_boundMaterial = info.Material;
            ++m_stats.MaterialBinds;
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
