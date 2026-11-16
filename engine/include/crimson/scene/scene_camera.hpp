#pragma once
#include <glm/glm.hpp>

#include "crimson/asset_manager/asset_id.hpp"
#include "crimson/renderer/camera_data.hpp"
#include "crimson/scene/entity.hpp"

namespace crimson
{
    // What the camera sees from where its world matrix puts it, looking along its -Z axis. The scale and shear of the
    // matrix don't change the view.
    [[nodiscard]] CameraView ComputeCameraView(const CameraComponent& camera, const glm::mat4& world, float aspect);

    // The scene's main camera: the referenced entity while it has a camera and is active, otherwise the first active
    // camera in hierarchy order, otherwise an invalid entity. Cameras inside instances only count for the second.
    [[nodiscard]] Entity FindMainCamera(Scene& scene, AssetID mainCamera);
}
