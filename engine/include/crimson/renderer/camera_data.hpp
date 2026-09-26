#pragma once
#include <glm/glm.hpp>

#include "renderer_api.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"

namespace crimson
{
    class PerspectiveCamera
    {
    public:
        PerspectiveCamera(const glm::vec3& position = glm::vec3(0.0f),float fov = 70.0f, float aspect = 16.0f / 9.0f, float near = 0.01f, float far = 1000.0f)
            : m_position(position), m_fov(fov), m_aspect(aspect), m_near(near), m_far(far)
        {
            RecalculateView();
            RecalculateProj();
        }

        void Move(const glm::vec3& direction)
        {
            m_position += direction;
            RecalculateView();
        }

        void SetPosition(const glm::vec3& value)
        {
            m_position = value;
            RecalculateView();
        }

        void SetAspect(float aspect)
        {
            m_aspect = aspect;
            RecalculateProj();
        }

        [[nodiscard]] glm::vec3 GetPosition() const { return m_position; }
        [[nodiscard]] glm::mat4 GetViewProj() const { return m_proj * m_view; }
    private:
        void RecalculateView()
        {
            m_view = glm::lookAt(m_position, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        }

        void RecalculateProj()
        {
            m_proj = RendererAPI::GetType() == RendererAPIType::OpenGL
                ? glm::perspectiveRH_NO(glm::radians(m_fov), m_aspect, m_near, m_far)
                : glm::perspectiveRH_ZO(glm::radians(m_fov), m_aspect, m_near, m_far);
        }
    private:
        glm::vec3 m_position;
        float m_fov;
        float m_aspect;
        float m_near;
        float m_far;

        glm::mat4 m_view;
        glm::mat4 m_proj;
    };
}
