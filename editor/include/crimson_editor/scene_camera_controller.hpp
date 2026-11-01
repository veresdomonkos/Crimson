#pragma once
#include <crimson/core/input.hpp>
#include <crimson/renderer/camera_data.hpp>
#include <crimson/scene/entity.hpp>

#include "crimson_editor/ui/viewport_panel.hpp"

namespace crimson::editor
{
    // Moves the scene camera with controls based on Unity's scene view
    class SceneCameraController
    {
    public:
        SceneCameraController(PerspectiveCamera& camera, Input& input, const ui::ViewportPanel& viewport);

        // Sets the point that orbiting goes around and zooming goes towards
        void SetPivot(const glm::vec3& pivot);

        // Moves the camera so the entity fills the view
        void Frame(Entity entity);

        void Update(float deltaTime);
    private:
        // A mouse gesture, which lasts as long as its button is held
        enum class Gesture { None, Fly, Pan, Orbit, Zoom };

        void BeginGesture();
        void EndGesture();

        void UpdateFly(float deltaTime);
        void UpdatePan();
        void UpdateOrbit();
        void UpdateZoom();
        void UpdateWithoutGesture(float deltaTime);
        void MoveWithKeys(float deltaTime);

        // Moves along the view direction, pushing the pivot ahead when getting close to it
        void Dolly(float amount);
        [[nodiscard]] float GetKeySpeed() const;
    private:
        PerspectiveCamera& m_camera;
        Input& m_input;
        const ui::ViewportPanel& m_viewport;

        Gesture m_gesture = Gesture::None;
        MouseButton m_gestureButton = MouseButton::Right;

        float m_pivotDistance = 5.0f;
        float m_flySpeed = 5.0f;
    };
}
