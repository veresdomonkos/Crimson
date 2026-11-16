#include "crimson_editor/scene_view.hpp"

#include <crimson/scene/scene_camera.hpp>

namespace crimson::editor
{
    bool SceneView::CanLookThrough(Entity entity) const
    {
        return entity && entity.HasComponent<CameraComponent>();
    }

    void SceneView::LookThrough(Entity camera)
    {
        m_lookThrough = CanLookThrough(camera) ? camera : Entity{};
    }

    void SceneView::Update()
    {
        if (!CanLookThrough(m_lookThrough))
            m_lookThrough = {};
    }

    CameraView SceneView::GetView() const
    {
        // Between a destroy and Update the entity may be gone, and then the editor camera is what is shown
        if (!CanLookThrough(m_lookThrough))
            return m_editorCamera.GetCameraView();

        return ComputeCameraView(m_lookThrough.GetComponent<CameraComponent>(), m_scene.ComputeWorldMatrix(m_lookThrough), m_aspect);
    }
}
