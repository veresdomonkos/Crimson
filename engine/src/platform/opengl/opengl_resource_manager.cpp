#include "opengl_resource_manager.hpp"

#include "utils.hpp"
#include "crimson/core/log.hpp"

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

    ShaderHandle OpenGLResourceManager::CreateShader(std::span<const uint32_t> vertexBinary, std::span<const uint32_t> fragmentBinary)
    {
        GLuint vertex = CompileSPIRVShader(GL_VERTEX_SHADER, vertexBinary, "Vertex");
        if (vertex == 0)
        {
            return ShaderHandle::Invalid();
        }

        GLuint fragment = CompileSPIRVShader(GL_FRAGMENT_SHADER, fragmentBinary, "Fragment");
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
        ReflectShader(shader);
        return m_shaders.Register(shader);
    }

    void OpenGLResourceManager::DestroyShader(ShaderHandle handle)
    {
        if (handle)
        {
            glDeleteProgram(m_shaders.Get(handle).GLHandle);
        }
    }

    MaterialHandle OpenGLResourceManager::CreateMaterial(ShaderHandle shaderHandle)
    {
        const OpenGLShader& shader = m_shaders.Get(shaderHandle);
        if (!shader.GLHandle)
        {
            LOG_ERROR("Cannot create material form invalid ShaderHandle!");
            return MaterialHandle::Invalid();
        }

        OpenGLMaterial mat;
        mat.Shader = shaderHandle;
        mat.UniformDataSize = shader.UBOSize;

        glCreateBuffers(1, &mat.GLBufferHandle);
        glNamedBufferData(mat.GLBufferHandle, static_cast<GLsizeiptr>(shader.UBOSize), nullptr, GL_DYNAMIC_DRAW);

        if (shader.UBOSize > 0)
        {
            mat.UniformData = std::make_unique<std::byte[]>(shader.UBOSize);
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

    OpenGLGraphicsPipeline OpenGLResourceManager::CreateGraphicsPipeline(const GraphicsPipelineInfo &info)
    {
        return OpenGLGraphicsPipeline{};
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

        auto it = shader.Properties.find(std::string(name));
        if (it == shader.Properties.end())
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

    void OpenGLResourceManager::ReflectShader(Shader &shader)
    {
        GLuint blockIndex = glGetUniformBlockIndex(shader.GLHandle, "MaterialBlock");
        if (blockIndex != GL_INVALID_INDEX)
        {
            // 1. A MaterialBlock teljes méretének lekérése bájtban
            GLint blockSize = 0;
            glGetActiveUniformBlockiv(shader.GLHandle, blockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
            shader.UBOSize = static_cast<std::size_t>(blockSize);

            // 2. A blokkban lévő aktív uniformok számának lekérése
            GLint numUniforms = 0;
            glGetActiveUniformBlockiv(shader.GLHandle, blockIndex, GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS, &numUniforms);

            if (numUniforms > 0)
            {
                // 3. A blokkhoz tartozó uniformok indexeinek lekérése
                std::vector<GLint> uniformIndices(numUniforms);
                glGetActiveUniformBlockiv(shader.GLHandle, blockIndex, GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES, uniformIndices.data());

                // 4. Az egyes uniformok offsetjeinek és típusainak lekérése
                std::vector<GLint> uniformOffsets(numUniforms);
                std::vector<GLint> uniformTypes(numUniforms);
                std::vector<GLint> uniformSizes(numUniforms); // tömbök elemszáma

                glGetActiveUniformsiv(shader.GLHandle, numUniforms, reinterpret_cast<const GLuint*>(uniformIndices.data()), GL_UNIFORM_OFFSET, uniformOffsets.data());
                glGetActiveUniformsiv(shader.GLHandle, numUniforms, reinterpret_cast<const GLuint*>(uniformIndices.data()), GL_UNIFORM_TYPE, uniformTypes.data());
                glGetActiveUniformsiv(shader.GLHandle, numUniforms, reinterpret_cast<const GLuint*>(uniformIndices.data()), GL_UNIFORM_SIZE, uniformSizes.data());

                // 5. Változónevek és metaadatok kinyerése
                for (int i = 0; i < numUniforms; ++i)
                {
                    char nameBuffer[256];
                    GLsizei length = 0;
                    glGetActiveUniformName(shader.GLHandle, uniformIndices[i], sizeof(nameBuffer), &length, nameBuffer);

                    std::string name(nameBuffer);

                    // Ha az OpenGL "MaterialBlock.u_Color" néven adná vissza, levágjuk a blokk nevét:
                    std::size_t dotPos = name.find_last_of('.');
                    if (dotPos != std::string::npos)
                    {
                        name = name.substr(dotPos + 1);
                    }

                    // Típus alapján méret becslése/meghatározása (bájtban)
                    std::size_t elementSize = utils::GetGLTypeSize(uniformTypes[i]);

                    shader.Properties[name] = ShaderPropertyInfo{
                        .Offset = static_cast<std::size_t>(uniformOffsets[i]),
                        .Size   = elementSize * uniformSizes[i]
                    };
                }
            }
        }
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
        }

        return shader;
    }

    GLuint OpenGLResourceManager::CompileSPIRVShader(GLenum type, std::span<const uint32_t> binary, std::string_view stageName)
    {
        GLuint shader = glCreateShader(type);

        glShaderBinary(1, &shader, GL_SHADER_BINARY_FORMAT_SPIR_V, binary.data(), static_cast<GLsizei>(binary.size_bytes()));
        glSpecializeShader(shader, "main", 0, nullptr, nullptr);

        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled)
        {
            char log[2048];
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            LOG_ERROR("{} Shader SPIR-V Specialization error: {}", stageName, log);
            glDeleteShader(shader);
            return 0;
        }

        return shader;
    }
}
