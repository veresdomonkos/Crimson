#include "opengl_resource_manager.hpp"

#include "utils.hpp"

namespace crimson::opengl
{
    RenderSurfaceHandle OpenGLResourceManager::CreateRenderSurface(const Window &window)
    {
        RenderTargetHandle backBuffer = m_renderTargets.Register(OpenGLRenderTarget{.Width = window.Width(), .Height = window.Height(), .FrameBufferHandle = 0});
        return  m_renderSurfaces.Register(OpenGLSurface{.WindowHandle = window.GetNativeHandle(), .BackBufferHandle = backBuffer});
    }

    VertexBufferHandle OpenGLResourceManager::CreateVertexBuffer(const VertexBufferInfo &info, const void* data)
    {
        OpenGLVertexBuffer vbo {
            .Layout = info.Layout,
            .Size = info.Size,
            .Usage = info.Usage,
        };

        glCreateBuffers(1, &vbo.GLHandle);
        glNamedBufferData(vbo.GLHandle, static_cast<GLsizeiptr>(info.Size), data, utils::GetGLBufferUsage(vbo.Usage));

        return m_vertexBuffers.Register(vbo);
    }

    void OpenGLResourceManager::DestroyVertexBuffer(VertexBufferHandle handle)
    {
        OpenGLVertexBuffer& vbo = m_vertexBuffers.Get(handle);
        glDeleteBuffers(1, &vbo.GLHandle);
        m_vertexBuffers.Unregister(handle);
    }

    IndexBufferHandle OpenGLResourceManager::CreateIndexBuffer(const IndexBufferInfo &info, const void *data)
    {
        OpenGLIndexBuffer ibo {
            .Size = info.Size,
            .Usage = info.Usage,
            .Type = info.Type,
        };

        glCreateBuffers(1, &ibo.GLHandle);
        glNamedBufferData(ibo.GLHandle, static_cast<GLsizeiptr>(info.Size), data, utils::GetGLBufferUsage(ibo.Usage));

        return m_indexBuffers.Register(ibo);
    }

    void OpenGLResourceManager::DestroyIndexBuffer(IndexBufferHandle handle)
    {
        OpenGLIndexBuffer& ibo = m_indexBuffers.Get(handle);
        glDeleteBuffers(1, &ibo.GLHandle);
        m_indexBuffers.Unregister(handle);
    }

    VertexArrayHandle OpenGLResourceManager::CreateVertexArray(const VertexArrayInfo &info)
    {
        VertexArray vao;

        glCreateVertexArrays(1, &vao.GLHandle);

        auto& vertexBuffer = m_vertexBuffers.Get(info.VertexBuffer);
        auto& indexBuffer = m_indexBuffers.Get(info.IndexBuffer);

        glVertexArrayElementBuffer(vao.GLHandle,indexBuffer.GLHandle);

        uint32_t location = 0;

        for (const auto& element : vertexBuffer.Layout)
        {
            glEnableVertexArrayAttrib(vao.GLHandle,location);

            glVertexArrayAttribFormat(
                vao.GLHandle,
                location,
                static_cast<GLint>(element.ComponentCount()),
                GL_FLOAT,
                false,//element.Normalized,
                element.Offset
            );

            glVertexArrayVertexBuffer(
                vao.GLHandle,
                0,
                vertexBuffer.GLHandle,
                0,
                static_cast<GLsizei>(vertexBuffer.Layout.GetStride())
            );

            glVertexArrayAttribBinding(
                vao.GLHandle,
                location,
                0
            );

            location++;
        }

        return m_vertexArrays.Register(vao);
    }

    OpenGLGraphicsPipeline OpenGLResourceManager::CreateGraphicsPipeline(const GraphicsPipelineInfo &info)
    {
        return OpenGLGraphicsPipeline{};
    }
}
