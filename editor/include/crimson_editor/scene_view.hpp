#pragma once
#include <crimson/renderer/camera_data.hpp>
#include <crimson/scene/entity.hpp>

namespace crimson::editor
{
    // What the Scene view is seen from: the editor camera, or a scene camera that is being looked through. The editor
    // camera is only read, so it is where it was when looking through ends.
    class SceneView
    {
    public:
        SceneView(Scene& scene, const PerspectiveCamera& editorCamera, float aspect)
            : m_scene(scene), m_editorCamera(editorCamera), m_aspect(aspect) {}

        // Only an entity with a CameraComponent, anything else stops looking through
        void LookThrough(Entity camera);
        void StopLookingThrough() { m_lookThrough = {}; }

        // Stops looking through a camera that was destroyed or lost its CameraComponent
        void Update();

        [[nodiscard]] bool IsLookingThrough() const { return static_cast<bool>(m_lookThrough); }
        [[nodiscard]] Entity GetLookThrough() const { return m_lookThrough; }

        // The camera's while looking through one, otherwise the editor camera's
        [[nodiscard]] CameraView GetView() const;
    private:
        [[nodiscard]] bool CanLookThrough(Entity entity) const;

        Scene& m_scene;
        const PerspectiveCamera& m_editorCamera;
        float m_aspect;
        Entity m_lookThrough;
    };
}
