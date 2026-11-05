#include "crimson_editor/scene_camera_controller.hpp"

#include <algorithm>
#include <cmath>

#include <imgui.h>

namespace crimson::editor
{
    static constexpr float kLookSensitivity = 0.1f;
    static constexpr float kFastMultiplier = 3.0f;
    static constexpr float kMinFlySpeed = 0.1f;
    static constexpr float kMaxFlySpeed = 200.0f;
    static constexpr float kFlySpeedStep = 1.2f;
    static constexpr float kDollyStep = 0.1f;
    static constexpr float kZoomSensitivity = 0.005f;
    static constexpr float kMinPivotDistance = 0.1f;

    SceneCameraController::SceneCameraController(PerspectiveCamera& camera, Input& input, const ui::ViewportPanel& viewport)
        : m_camera(camera), m_input(input), m_viewport(viewport)
    {
    }

    void SceneCameraController::SetPivot(const glm::vec3& pivot)
    {
        m_pivotDistance = std::max(glm::length(pivot - m_camera.GetPosition()), kMinPivotDistance);
        m_camera.LookAt(pivot);
    }

    void SceneCameraController::Frame(Entity entity)
    {
        constexpr float kDefaultRadius = 1.0f;
        constexpr float kMargin = 1.2f;

        const glm::mat4& world = entity.GetComponent<TransformComponent>().World;

        glm::vec3 center(world[3]);
        float radius = kDefaultRadius;

        const auto* renderer = entity.TryGetComponent<MeshRendererComponent>();
        if (renderer && !renderer->Resolved.SubMeshes.empty())
        {
            const MeshBounds& bounds = renderer->Resolved.Bounds;
            const float scale = std::max({ glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])) });

            center = glm::vec3(world * glm::vec4((bounds.Min + bounds.Max) * 0.5f, 1.0f));
            radius = glm::length(bounds.Max - bounds.Min) * 0.5f * scale;
        }

        m_pivotDistance = std::max(radius * kMargin / std::sin(glm::radians(m_camera.GetFov()) * 0.5f), kMinPivotDistance);
        m_camera.SetPosition(center - m_camera.GetForward() * m_pivotDistance);
    }

    void SceneCameraController::Update(float deltaTime)
    {
        if (m_gesture == Gesture::None)
            BeginGesture();
        else if (!m_input.IsMouseButtonDown(m_gestureButton))
            EndGesture();

        switch (m_gesture)
        {
            case Gesture::Fly:   UpdateFly(deltaTime); break;
            case Gesture::Pan:   UpdatePan(); break;
            case Gesture::Orbit: UpdateOrbit(); break;
            case Gesture::Zoom:  UpdateZoom(); break;
            case Gesture::None:  UpdateWithoutGesture(deltaTime); break;
        }
    }

    void SceneCameraController::BeginGesture()
    {
        if (!m_viewport.IsHovered())
            return;

        const bool alt = m_input.IsKeyDown(Key::LeftAlt) || m_input.IsKeyDown(Key::RightAlt);

        const auto begin = [this](Gesture gesture, MouseButton button)
        {
            m_gesture = gesture;
            m_gestureButton = button;
            m_input.SetCursorLocked(true);
        };

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            begin(Gesture::Pan, MouseButton::Middle);
        else if (alt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            begin(Gesture::Orbit, MouseButton::Left);
        else if (alt && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            begin(Gesture::Zoom, MouseButton::Right);
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            begin(Gesture::Fly, MouseButton::Right);
    }

    void SceneCameraController::EndGesture()
    {
        m_gesture = Gesture::None;
        m_input.SetCursorLocked(false);
    }

    void SceneCameraController::UpdateFly(float deltaTime)
    {
        const glm::vec2 mouse = m_input.GetMouseDelta();
        m_camera.Rotate(mouse.x * kLookSensitivity, -mouse.y * kLookSensitivity);

        // The wheel changes the speed while flying
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
            m_flySpeed = std::clamp(m_flySpeed * std::pow(kFlySpeedStep, wheel), kMinFlySpeed, kMaxFlySpeed);

        MoveWithKeys(deltaTime);
    }

    void SceneCameraController::UpdatePan()
    {
        // Scaled so the point at the pivot's distance stays under the mouse
        const float height = std::max(m_viewport.GetImageSize().y, 1.0f);
        const float unitsPerPixel = 2.0f * m_pivotDistance * std::tan(glm::radians(m_camera.GetFov()) * 0.5f) / height;

        const glm::vec2 mouse = m_input.GetMouseDelta();
        m_camera.Move((m_camera.GetUp() * mouse.y - m_camera.GetRight() * mouse.x) * unitsPerPixel);
    }

    void SceneCameraController::UpdateOrbit()
    {
        const glm::vec3 pivot = m_camera.GetPosition() + m_camera.GetForward() * m_pivotDistance;

        const glm::vec2 mouse = m_input.GetMouseDelta();
        m_camera.Rotate(mouse.x * kLookSensitivity, -mouse.y * kLookSensitivity);

        m_camera.SetPosition(pivot - m_camera.GetForward() * m_pivotDistance);
    }

    void SceneCameraController::UpdateZoom()
    {
        const glm::vec2 mouse = m_input.GetMouseDelta();
        Dolly((mouse.x - mouse.y) * kZoomSensitivity * m_pivotDistance);
    }

    void SceneCameraController::UpdateWithoutGesture(float deltaTime)
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f && m_viewport.IsHovered())
            Dolly(wheel * kDollyStep * m_pivotDistance);

        // The move keys belong to the scene while its panel is focused, and are left alone otherwise
        if (m_viewport.IsFocused() && !ImGui::GetIO().WantTextInput)
            MoveWithKeys(deltaTime);
    }

    void SceneCameraController::MoveWithKeys(float deltaTime)
    {
        glm::vec3 direction(0.0f);

        if (m_input.IsKeyDown(Key::W) || m_input.IsKeyDown(Key::Up))    direction += m_camera.GetForward();
        if (m_input.IsKeyDown(Key::S) || m_input.IsKeyDown(Key::Down))  direction -= m_camera.GetForward();
        if (m_input.IsKeyDown(Key::D) || m_input.IsKeyDown(Key::Right)) direction += m_camera.GetRight();
        if (m_input.IsKeyDown(Key::A) || m_input.IsKeyDown(Key::Left))  direction -= m_camera.GetRight();
        if (m_input.IsKeyDown(Key::E)) direction += m_camera.GetUp();
        if (m_input.IsKeyDown(Key::Q)) direction -= m_camera.GetUp();

        // Two keys at once would otherwise add up to a faster diagonal
        if (glm::length(direction) > 0.0f)
            direction = glm::normalize(direction);

        m_camera.Move(direction * GetKeySpeed() * deltaTime);
    }

    void SceneCameraController::Dolly(float amount)
    {
        m_camera.Move(m_camera.GetForward() * amount);
        m_pivotDistance = std::max(m_pivotDistance - amount, kMinPivotDistance);
    }

    float SceneCameraController::GetKeySpeed() const
    {
        const bool fast = m_input.IsKeyDown(Key::LeftShift) || m_input.IsKeyDown(Key::RightShift);
        return fast ? m_flySpeed * kFastMultiplier : m_flySpeed;
    }
}
