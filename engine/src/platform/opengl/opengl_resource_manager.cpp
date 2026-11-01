#include "opengl_resource_manager.hpp"

#include <algorithm>
#include <string_view>

#include "spirv_to_glsl.hpp"
#include "utils.hpp"
#include "crimson/core/log.hpp"

namespace crimson::opengl
{
    OpenGLResourceManager::OpenGLResourceManager(Window& window)
    {
        OpenGLRenderTarget rt {
            .ColorAttachments = {},
            .DepthAttachment = TextureHandle::Invalid(),
            .Width = window.Width(),
            .Height = window.Height(),
            .FrameBufferHandle = 0
        };

        m_backBufferHandle = m_renderTargets.Register(rt);

        const auto createSampler = [](GLint filter, GLint addressMode, bool compare)
        {
            GLuint sampler = 0;
            glCreateSamplers(1, &sampler);
            glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, filter == GL_LINEAR ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST);
            glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, filter);
            glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, addressMode);
            glSamplerParameteri(sampler, GL_TEXTURE_WRAP_T, addressMode);
            glSamplerParameteri(sampler, GL_TEXTURE_WRAP_R, addressMode);

            if (compare)
            {
                glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
                glSamplerParameteri(sampler, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
            }

            return sampler;
        };

        m_linearWrapSampler = createSampler(GL_LINEAR, GL_REPEAT, false);
        m_linearClampSampler = createSampler(GL_LINEAR, GL_CLAMP_TO_EDGE, false);
        m_pointClampSampler = createSampler(GL_NEAREST, GL_CLAMP_TO_EDGE, false);
        m_shadowCompareSampler = createSampler(GL_LINEAR, GL_CLAMP_TO_EDGE, true);

        constexpr uint32_t white = 0xFFFFFFFF;

        const TextureInfo defaultInfo{ .Width = 1, .Height = 1, .Format = TextureFormat::RGBA8, .Usage = TextureUsage::Sampled };
        m_defaultTexture = CreateTexture(defaultInfo, &white);
    }

    GLuint OpenGLResourceManager::GetSharedSampler(std::string_view name) const
    {
        if (name == "LinearClamp") return m_linearClampSampler;
        if (name == "PointClamp") return m_pointClampSampler;
        if (name == "ShadowCompare") return m_shadowCompareSampler;
        return m_linearWrapSampler;
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
        std::erase_if(m_vertexArrays, [&](const auto& entry)
        {
            if (entry.first.VertexBuffer != handle)
                return false;

            glDeleteVertexArrays(1, &entry.second);
            return true;
        });

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
        std::erase_if(m_vertexArrays, [&](const auto& entry)
        {
            if (entry.first.IndexBuffer != handle)
                return false;

            glDeleteVertexArrays(1, &entry.second);
            return true;
        });

        OpenGLIndexBuffer& ibo = m_indexBuffers.Get(handle);
        glDeleteBuffers(1, &ibo.GLHandle);
        m_indexBuffers.Unregister(handle);
    }

    GLuint OpenGLResourceManager::GetOrCreateVertexArray(VertexBufferHandle vertexBufferHandle, IndexBufferHandle indexBufferHandle)
    {
        auto [it, inserted] = m_vertexArrays.try_emplace(VertexArrayKey{ vertexBufferHandle, indexBufferHandle }, 0);

        if (!inserted)
            return it->second;

        GLuint vao = 0;
        glCreateVertexArrays(1, &vao);
        it->second = vao;

        auto& vertexBuffer = m_vertexBuffers.Get(vertexBufferHandle);
        auto& indexBuffer = m_indexBuffers.Get(indexBufferHandle);

        glVertexArrayElementBuffer(vao,indexBuffer.GLHandle);

        uint32_t location = 0;

        for (const auto& element : vertexBuffer.Layout)
        {
            glEnableVertexArrayAttrib(vao,location);

            glVertexArrayAttribFormat(
                vao,
                location,
                static_cast<GLint>(element.ComponentCount()),
                GL_FLOAT,
                false,//element.Normalized,
                element.Offset
            );

            glVertexArrayVertexBuffer(
                vao,
                0,
                vertexBuffer.GLHandle,
                0,
                static_cast<GLsizei>(vertexBuffer.Layout.GetStride())
            );

            glVertexArrayAttribBinding(
                vao,
                location,
                0
            );

            location++;
        }

        return vao;
    }

    ShaderHandle OpenGLResourceManager::CreateShader(const ShaderBinary& binary)
    {
        auto shaderInterface = ReflectShaderInterface(binary);
        if (!shaderInterface) return ShaderHandle::Invalid();

        std::vector<GlslTextureUnit> textureUnits;

        const auto vertexSource = TranslateSpirvToGlsl(binary.Vertex, textureUnits);
        const auto fragmentSource = TranslateSpirvToGlsl(binary.Pixel, textureUnits);
        if (!vertexSource || !fragmentSource) return ShaderHandle::Invalid();

        GLuint vertex = CompileShader(GL_VERTEX_SHADER, *vertexSource);
        if (vertex == 0) return ShaderHandle::Invalid();

        GLuint fragment = CompileShader(GL_FRAGMENT_SHADER, *fragmentSource);
        if (fragment == 0)
        {
            glDeleteShader(vertex);
            return ShaderHandle::Invalid();
        }

        GLuint program = glCreateProgram();
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);

        GLint success = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success)
        {
            char log[2048];
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            LOG_ERROR("Shader link error: {}", log);

            glDeleteProgram(program);
            glDeleteShader(vertex);
            glDeleteShader(fragment);
            return ShaderHandle::Invalid();
        }

        glDeleteShader(vertex);
        glDeleteShader(fragment);

        OpenGLShader shader {.GLHandle = program};
        shader.Interface = std::move(*shaderInterface);
        shader.ObjectIndexLocation = glGetUniformLocation(program, fmt::format("{}.ObjectIndex", kDrawUniformName).c_str());

        for (const GlslTextureUnit& unit : textureUnits)
        {
            shader.TextureUnits.push_back({
                .Texture = unit.Texture,
                .Sampler = GetSharedSampler(unit.Sampler),
                .Unit = unit.Unit,
                .IsShadowAtlas = unit.Texture == "ShadowAtlas"
            });
        }

        return m_shaders.Register(std::move(shader));
    }

    void OpenGLResourceManager::DestroyShader(ShaderHandle handle)
    {
        if (handle)
        {
            glDeleteProgram(m_shaders.Get(handle).GLHandle);
            m_shaders.Unregister(handle);
        }
    }

    MaterialHandle OpenGLResourceManager::CreateMaterial(ShaderHandle shaderHandle, const MaterialSettings& settings)
    {
        const OpenGLShader& shader = m_shaders.Get(shaderHandle);
        if (!shader.GLHandle)
        {
            LOG_ERROR("Cannot create material form invalid ShaderHandle!");
            return MaterialHandle::Invalid();
        }

        OpenGLMaterial mat;
        mat.Shader = shaderHandle;
        mat.Settings = settings;
        mat.UniformDataSize = shader.Interface.MaterialBlockSize;

        if (mat.UniformDataSize > 0)
        {
            glCreateBuffers(1, &mat.GLBufferHandle);
            glNamedBufferData(mat.GLBufferHandle, static_cast<GLsizeiptr>(mat.UniformDataSize), nullptr, GL_DYNAMIC_DRAW);
            mat.UniformData = std::make_unique<std::byte[]>(mat.UniformDataSize);
        }

        return m_materials.Register(std::move(mat));
    }

    void OpenGLResourceManager::DestroyMaterial(MaterialHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }

        OpenGLMaterial& mat = m_materials.Get(handle);

        if (mat.GLBufferHandle != 0)
        {
            glDeleteBuffers(1, &mat.GLBufferHandle);
            mat.GLBufferHandle = 0;
        }

        m_materials.Unregister(handle);
    }

    void OpenGLResourceManager::SetMaterialTexture(MaterialHandle material, std::string_view name, TextureHandle texture)
    {
        OpenGLMaterial& mat = m_materials.Get(material);
        if (mat.Shader == ShaderHandle::Invalid())
        {
            LOG_WARN("Invalid MaterialHandle in SetMaterialTexture!");
            return;
        }

        const OpenGLShader& shader = m_shaders.Get(mat.Shader);
        auto it = shader.Interface.Textures.find(std::string(name));
        if (it == shader.Interface.Textures.end())
        {
            LOG_WARN("Texture property '{}' not found in Shader!", name);
            return;
        }

        if (!texture)
        {
            LOG_WARN("Invalid TextureHandle passed for '{}'!", name);
            return;
        }

        mat.Textures[it->first] = m_textures.Get(texture).GLHandle;
    }

    void OpenGLResourceManager::SetMaterialPropertyByNameImpl(MaterialHandle handle, std::string_view name, std::span<const std::byte> data)
    {
        OpenGLMaterial& mat = m_materials.Get(handle);
        if (mat.Shader == ShaderHandle::Invalid())
        {
            LOG_WARN("Invalid MaterialHandle in SetMaterialPropertyByNameImpl!");
            return;
        }

        const OpenGLShader& shader = m_shaders.Get(mat.Shader);

        auto it = shader.Interface.Properties.find(std::string(name));
        if (it == shader.Interface.Properties.end())
        {
            LOG_WARN("Uniform property '{}' not found in Shader!", name);
            return;
        }

        const auto& propInfo = it->second;

        if (data.size() > propInfo.Size)
        {
            LOG_WARN("Data size ({}) exceeds property size ({}) for '{}'!", data.size(), propInfo.Size, name);
            return;
        }

        if (!mat.UniformData || (propInfo.Offset + data.size() > mat.UniformDataSize))
        {
            LOG_ERROR("Material buffer overflow writing property '{}'!", name);
            return;
        }

        std::copy_n(data.data(), data.size(), mat.UniformData.get() + propInfo.Offset);
        mat.IsDirty = true;
    }

    GLuint OpenGLResourceManager::CompileShader(GLenum type, std::string_view source)
    {
        GLuint shader = glCreateShader(type);

        const char* src = source.data();
        GLint length = static_cast<GLint>(source.size());
        glShaderSource(shader,1, &src, &length);
        glCompileShader(shader);
        GLint success = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);

        if (!success)
        {
            char log[2048];
            glGetShaderInfoLog(
                shader,
                sizeof(log),
                nullptr,
                log
            );

            glDeleteShader(shader);
            LOG_ERROR("Shader compile error: {}", log);
            return 0;
        }

        return shader;
    }

    TextureHandle OpenGLResourceManager::CreateTexture(const TextureInfo& info, const void* data)
    {
        OpenGLTexture tex{};
        tex.Width  = info.Width;
        tex.Height = info.Height;
        tex.InternalFormat = utils::GetGLInternalFormat(info.Format);

        glCreateTextures(GL_TEXTURE_2D, 1, &tex.GLHandle);

        const auto mipLevels = static_cast<GLsizei>(info.MipLevels > 0 ? info.MipLevels : 1);
        glTextureStorage2D(tex.GLHandle, mipLevels, tex.InternalFormat, static_cast<GLsizei>(info.Width), static_cast<GLsizei>(info.Height));

        if (data != nullptr)
        {
            glTextureSubImage2D(
                tex.GLHandle, 0, 0, 0,
                 static_cast<GLsizei>(info.Width), static_cast<GLsizei>(info.Height),
                 utils::GetGLUploadFormat(info.Format),
                 utils::GetGLUploadType(info.Format),
                 data
            );

            if (mipLevels > 1)
                glGenerateTextureMipmap(tex.GLHandle);
        }

        glTextureParameteri(tex.GLHandle, GL_TEXTURE_MIN_FILTER, mipLevels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTextureParameteri(tex.GLHandle, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(tex.GLHandle, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(tex.GLHandle, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        return m_textures.Register(tex);
    }

    void OpenGLResourceManager::DestroyTexture(TextureHandle handle)
    {
        if (!handle) return;

        OpenGLTexture& tex = m_textures.Get(handle);
        glDeleteTextures(1, &tex.GLHandle);
        m_textures.Unregister(handle);
    }

    RenderTargetHandle OpenGLResourceManager::CreateRenderTarget(const RenderTargetInfo& info)
    {
        OpenGLRenderTarget rt{};
        rt.Width  = info.Width;
        rt.Height = info.Height;

        glCreateFramebuffers(1, &rt.FrameBufferHandle);

        std::vector<GLenum> drawBuffers;
        drawBuffers.reserve(info.ColorFormats.size());

        for (std::size_t i = 0; i < info.ColorFormats.size(); ++i)
        {
            const TextureInfo colorInfo {
                .Width = info.Width, .Height = info.Height,
                .Format = info.ColorFormats[i],
                .Usage = TextureUsage::ColorAttachment | TextureUsage::Sampled,
                .MipLevels = 1,
            };

            const TextureHandle colorHandle = CreateTexture(colorInfo, nullptr);
            rt.ColorAttachments.push_back(colorHandle);

            const GLenum attachment = GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i);
            glNamedFramebufferTexture(rt.FrameBufferHandle, attachment, m_textures.Get(colorHandle).GLHandle, 0);
            drawBuffers.push_back(attachment);
        }

        if (info.DepthFormat)
        {
            const TextureInfo depthInfo {
                .Width = info.Width, .Height = info.Height,
                .Format = *info.DepthFormat,
                .Usage = TextureUsage::DepthStencilAttachment,
                .MipLevels = 1,
            };

            const TextureHandle depthHandle = CreateTexture(depthInfo, nullptr);
            rt.DepthAttachment = depthHandle;

            const GLenum attachment = utils::HasStencil(*info.DepthFormat) ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
            glNamedFramebufferTexture(rt.FrameBufferHandle, attachment, m_textures.Get(depthHandle).GLHandle, 0);
        }

        if (drawBuffers.empty())
        {
            glNamedFramebufferDrawBuffer(rt.FrameBufferHandle, GL_NONE);
            glNamedFramebufferReadBuffer(rt.FrameBufferHandle, GL_NONE);
        }
        else
        {
            glNamedFramebufferDrawBuffers(rt.FrameBufferHandle, static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
        }

        if (glCheckNamedFramebufferStatus(rt.FrameBufferHandle, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            LOG_ERROR("Framebuffer incomplete!");
            for (auto h : rt.ColorAttachments) DestroyTexture(h);
            if (rt.DepthAttachment) DestroyTexture(rt.DepthAttachment);
            glDeleteFramebuffers(1, &rt.FrameBufferHandle);
            return RenderTargetHandle::Invalid();
        }

        return m_renderTargets.Register(std::move(rt));
    }

    void OpenGLResourceManager::DestroyRenderTarget(RenderTargetHandle handle)
    {
        if (!handle) return;

        OpenGLRenderTarget& rt = m_renderTargets.Get(handle);
        for (auto h : rt.ColorAttachments) DestroyTexture(h);
        if (rt.DepthAttachment) DestroyTexture(rt.DepthAttachment);

        glDeleteFramebuffers(1, &rt.FrameBufferHandle);
        m_renderTargets.Unregister(handle);
    }

    TextureHandle OpenGLResourceManager::GetColorAttachment(RenderTargetHandle handle, uint32_t index) const
    {
        return m_renderTargets.Get(handle).ColorAttachments.at(index);
    }

    std::optional<TextureHandle> OpenGLResourceManager::GetDepthAttachment(RenderTargetHandle handle) const
    {
        return m_renderTargets.Get(handle).DepthAttachment;
    }
}
