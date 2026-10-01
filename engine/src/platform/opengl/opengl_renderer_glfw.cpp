#include "opengl_renderer.hpp"
#include "utils.hpp"
#include "crimson/core/log.hpp"

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
        auto* window = m_device.GetPrimaryWindow();
        glfwMakeContextCurrent(window);

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
        Frame& frame = m_frames[frameContext.GetIndex()];
        auto* window = m_device.GetPrimaryWindow();
        glfwMakeContextCurrent(window);

        for (const auto& entry : frame.GetPasses())
        {
            if (const auto* materialPass = std::get_if<RenderPass>(&entry))
            {
                ExecuteBeginRenderPass(materialPass->Info());
                for (const auto& draw : materialPass->GetDraws())
                    ExecuteDraw(draw);
            }
            else if (const auto* rawPass = std::get_if<RawPass>(&entry))
            {
                const auto& target = m_resourceManager.GetRenderTarget(rawPass->Target());
                glBindFramebuffer(GL_FRAMEBUFFER, target.FrameBufferHandle);
                glViewport(0, 0, static_cast<GLsizei>(target.Width), static_cast<GLsizei>(target.Height));

                NativeFrameHandles handles{};
                rawPass->Callback()(handles);
            }
        }

        glfwSwapBuffers(window);
    }

    void OpenGLRenderer::ExecuteBeginRenderPass(const RenderPassInfo& info)
    {
        CameraBlock block{};
        block.ViewProj = info.ViewProj;
        block.Position = glm::vec4(info.CameraPosition, 0.0f);
        glNamedBufferSubData(m_cameraUBO, 0, sizeof(CameraBlock), &block);

        const auto& target = m_resourceManager.GetRenderTarget(info.Target);
        glBindFramebuffer(GL_FRAMEBUFFER, target.FrameBufferHandle);
        glViewport(0, 0, static_cast<GLsizei>(target.Width), static_cast<GLsizei>(target.Height));

        GLbitfield clearMask = 0;
        if (HasClearFlag(info.ClearFlags, ClearFlags::Color))
        {
            glClearColor(info.ClearColor.r, info.ClearColor.g, info.ClearColor.b, info.ClearColor.a);
            clearMask |= GL_COLOR_BUFFER_BIT;
        }
        if (HasClearFlag(info.ClearFlags, ClearFlags::Depth))
        {
            glClearDepth(info.ClearDepth);
            clearMask |= GL_DEPTH_BUFFER_BIT;
        }
        if (HasClearFlag(info.ClearFlags, ClearFlags::Stencil))
        {
            glClearStencil(static_cast<GLint>(info.ClearStencil));
            clearMask |= GL_STENCIL_BUFFER_BIT;
        }
        if (clearMask != 0) glClear(clearMask);
    }

    void OpenGLRenderer::ExecuteDraw(const DrawInfo& info)
    {
        OpenGLVertexBuffer& vertexBuffer = m_resourceManager.GetVertexBuffer(info.VertexBuffer);
        OpenGLIndexBuffer& indexBuffer = m_resourceManager.GetIndexBuffer(info.IndexBuffer);
        OpenGLMaterial& material = m_resourceManager.GetMaterial(info.Material);
        OpenGLShader& shader = m_resourceManager.GetShader(material.Shader);

        auto& pipeline = m_resourceManager.GetOrCreateGraphicsPipeline({.Layout = vertexBuffer.Layout});

        VertexArrayHandle vertexArrayHandle;
        VertexArrayInfo vertexArrayInfo{ .VertexBuffer = info.VertexBuffer, .IndexBuffer = info.IndexBuffer };

        if (auto it = pipeline.VAOCache.find(vertexArrayInfo); it != pipeline.VAOCache.end())
            vertexArrayHandle = it->second;
        else
        {
            vertexArrayHandle = m_resourceManager.CreateVertexArray(vertexArrayInfo);
            pipeline.VAOCache.emplace(vertexArrayInfo, vertexArrayHandle);
        }

        VertexArray& vertexArray = m_resourceManager.GetVertexArray(vertexArrayHandle);

        if (material.GLBufferHandle != 0 && shader.UBOSize > 0)
        {
            if (material.IsDirty)
            {
                glNamedBufferSubData(material.GLBufferHandle, 0, static_cast<GLsizeiptr>(material.UniformDataSize), material.UniformData.get());
                material.IsDirty = false;
            }
            glBindBufferBase(GL_UNIFORM_BUFFER, shader.MaterialUboBinding, material.GLBufferHandle);
        }

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        for (const auto& [bindingUnit, glTexture] : material.BoundTextures)
            glBindTextureUnit(bindingUnit, glTexture);

        glUseProgram(shader.GLHandle);
        glBindVertexArray(vertexArray.GLHandle);

        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexBuffer.Size / Index::Size(indexBuffer.Type)), utils::GetGLIndexType(indexBuffer.Type), nullptr);
    }
}
