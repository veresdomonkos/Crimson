#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <glad/glad.h>

#include "crimson/renderer/core.hpp"
#include "crimson/renderer/material_settings.hpp"

namespace crimson::opengl
{
    // Remembers what is bound so nothing gets set to the value it already has.
    // Call Reset() whenever other code may have changed GL state, such as ImGui.
    class OpenGLStateCache
    {
    public:
        OpenGLStateCache() { Reset(); }

        void Reset()
        {
            m_framebuffer = kUnknown;
            m_program = kUnknown;
            m_vertexArray = kUnknown;
            m_uniformBuffers.fill(kUnknown);
            m_storageBuffers.fill(kUnknown);
            m_textures.fill(kUnknown);
            m_samplers.fill(kUnknown);
            m_scissor = Toggle::Unknown;
            m_depthTest = Toggle::Unknown;
            m_depthWrite = Toggle::Unknown;
            m_blend = Toggle::Unknown;
            m_cullKnown = false;
        }

        void BindFramebuffer(GLuint framebuffer)
        {
            if (m_framebuffer == framebuffer) return;
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            m_framebuffer = framebuffer;
        }

        bool UseProgram(GLuint program)
        {
            if (m_program == program) return false;
            glUseProgram(program);
            m_program = program;
            return true;
        }

        bool BindVertexArray(GLuint vertexArray)
        {
            if (m_vertexArray == vertexArray) return false;
            glBindVertexArray(vertexArray);
            m_vertexArray = vertexArray;
            return true;
        }

        void BindUniformBuffer(uint32_t binding, GLuint buffer)
        {
            if (binding < m_uniformBuffers.size())
            {
                if (m_uniformBuffers[binding] == buffer) return;
                m_uniformBuffers[binding] = buffer;
            }

            glBindBufferBase(GL_UNIFORM_BUFFER, binding, buffer);
        }

        void BindStorageBuffer(uint32_t binding, GLuint buffer)
        {
            if (binding < m_storageBuffers.size())
            {
                if (m_storageBuffers[binding] == buffer) return;
                m_storageBuffers[binding] = buffer;
            }

            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, buffer);
        }

        void BindTexture(uint32_t unit, GLuint texture)
        {
            if (unit < m_textures.size())
            {
                if (m_textures[unit] == texture) return;
                m_textures[unit] = texture;
            }

            glBindTextureUnit(unit, texture);
        }

        void BindSampler(uint32_t unit, GLuint sampler)
        {
            if (unit < m_samplers.size())
            {
                if (m_samplers[unit] == sampler) return;
                m_samplers[unit] = sampler;
            }

            glBindSampler(unit, sampler);
        }

        // Limits drawing and clearing to the rect, or lifts the limit when it is null
        void SetScissor(const RenderRect* rect)
        {
            if (rect)
                glScissor(rect->X, rect->Y, static_cast<GLsizei>(rect->Width), static_cast<GLsizei>(rect->Height));

            if (!Update(m_scissor, rect != nullptr)) return;

            if (rect)
                glEnable(GL_SCISSOR_TEST);
            else
                glDisable(GL_SCISSOR_TEST);
        }

        void SetDepthTest(bool enabled)
        {
            if (!Update(m_depthTest, enabled)) return;

            if (enabled)
            {
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LEQUAL);
            }
            else
            {
                glDisable(GL_DEPTH_TEST);
            }
        }

        void SetDepthWrite(bool enabled)
        {
            if (!Update(m_depthWrite, enabled)) return;
            glDepthMask(enabled ? GL_TRUE : GL_FALSE);
        }

        void SetBlend(bool enabled)
        {
            if (!Update(m_blend, enabled)) return;

            if (enabled)
            {
                glEnable(GL_BLEND);
                // The alpha factors keep an opaque target's alpha at 1, so the viewport doesn't show through
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            }
            else
            {
                glDisable(GL_BLEND);
            }
        }

        void SetCull(CullMode mode)
        {
            if (m_cullKnown && m_cull == mode) return;

            if (mode == CullMode::None)
            {
                glDisable(GL_CULL_FACE);
            }
            else
            {
                glEnable(GL_CULL_FACE);
                glCullFace(mode == CullMode::Back ? GL_BACK : GL_FRONT);
            }

            m_cull = mode;
            m_cullKnown = true;
        }

    private:
        enum class Toggle : uint8_t { Unknown, Off, On };

        static bool Update(Toggle& current, bool enabled)
        {
            const Toggle next = enabled ? Toggle::On : Toggle::Off;
            if (current == next) return false;
            current = next;
            return true;
        }

        static constexpr GLuint kUnknown = std::numeric_limits<GLuint>::max();

        GLuint m_framebuffer = kUnknown;
        GLuint m_program = kUnknown;
        GLuint m_vertexArray = kUnknown;
        std::array<GLuint, 32> m_uniformBuffers{};
        std::array<GLuint, 8> m_storageBuffers{};
        std::array<GLuint, 32> m_textures{};
        std::array<GLuint, 32> m_samplers{};
        Toggle m_scissor = Toggle::Unknown;
        Toggle m_depthTest = Toggle::Unknown;
        Toggle m_depthWrite = Toggle::Unknown;
        Toggle m_blend = Toggle::Unknown;
        CullMode m_cull = CullMode::Back;
        bool m_cullKnown = false;
    };
}
