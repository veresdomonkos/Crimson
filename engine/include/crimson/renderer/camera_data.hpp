#pragma once
#include <glm/glm.hpp>

#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"

namespace crimson
{
    class PerspectiveCamera
    {
    public:
        PerspectiveCamera(const glm::vec3& position = glm::vec3(0.0f), float fov = 70.0f, float aspect = 16.0f / 9.0f, float near = 0.01f, float far = 1000.0f)
            : m_position(position), m_fov(fov), m_aspect(aspect), m_near(near), m_far(far), m_yaw(-90.0f), m_pitch(0.0f)
        {
            RecalculateView();
            RecalculateProj();
        }

        void Move(const glm::vec3& direction)
        {
            m_position += direction;
            RecalculateView();
        }

        void MoveForward(float amount) { Move(GetForward() * amount); }

        void MoveRight(float amount) { Move(GetRight() * amount); }

        void SetPosition(const glm::vec3& value)
        {
            m_position = value;
            RecalculateView();
        }

        void Rotate(float yawDelta, float pitchDelta)
        {
            m_yaw += yawDelta;
            m_pitch = glm::clamp(m_pitch + pitchDelta, -89.9f, 89.9f);
            RecalculateView();
        }

        void SetRotation(float yaw, float pitch)
        {
            m_yaw = yaw;
            m_pitch = glm::clamp(pitch, -89.9f, 89.9f);
            RecalculateView();
        }

        void LookAt(const glm::vec3& target)
        {
            glm::vec3 direction = glm::normalize(target - m_position);
            m_pitch = glm::degrees(std::asin(glm::clamp(direction.y, -1.0f, 1.0f)));
            m_yaw = glm::degrees(std::atan2(direction.z, direction.x));
            RecalculateView();
        }

        void LookAlong(const glm::vec3& direction)
        {
            glm::vec3 forward = glm::normalize(direction);
            m_pitch = glm::degrees(std::asin(glm::clamp(forward.y, -1.0f, 1.0f)));
            m_yaw = glm::degrees(std::atan2(forward.z, forward.x));
            RecalculateView();
        }

        void SetAspect(float aspect)
        {
            m_aspect = aspect;
            RecalculateProj();
        }

        [[nodiscard]] glm::vec3 GetPosition() const { return m_position; }
        [[nodiscard]] glm::vec3 GetForward() const
        {
            glm::vec3 forward;
            forward.x = std::cos(glm::radians(m_yaw)) * std::cos(glm::radians(m_pitch));
            forward.y = std::sin(glm::radians(m_pitch));
            forward.z = std::sin(glm::radians(m_yaw)) * std::cos(glm::radians(m_pitch));
            return glm::normalize(forward);
        }

        [[nodiscard]] glm::vec3 GetRight() const
        {
            return glm::normalize(glm::cross(GetForward(), glm::vec3(0.0f, 1.0f, 0.0f)));
        }

        [[nodiscard]] glm::vec3 GetUp() const
        {
            return glm::normalize(glm::cross(GetRight(), GetForward()));
        }

        [[nodiscard]] float GetYaw() const { return m_yaw; }
        [[nodiscard]] float GetPitch() const { return m_pitch; }
        [[nodiscard]] glm::mat4 GetViewProj() const { return m_proj * m_view; }

    private:
        void RecalculateView()
        {
            m_view = glm::lookAt(m_position, m_position + GetForward(), GetUp());
        }

        void RecalculateProj()
        {
            m_proj = glm::perspectiveRH_ZO(glm::radians(m_fov), m_aspect, m_near, m_far);
        }

    private:
        glm::vec3 m_position;
        float m_fov;
        float m_aspect;
        float m_near;
        float m_far;
        float m_yaw;
        float m_pitch;
        glm::mat4 m_view;
        glm::mat4 m_proj;
    };
}
