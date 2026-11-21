#pragma once
#include <optional>

#include <glm/mat4x4.hpp>

#include "crimson_editor/ui/types.hpp"

namespace crimson::editor::ui
{
    enum class GizmoTool
    {
        Move,
        Rotate,
        Scale
    };

    enum class GizmoSpace
    {
        Local,
        World
    };

    struct GizmoOptions
    {
        GizmoTool Tool = GizmoTool::Move;
        GizmoSpace Space = GizmoSpace::Local;

        // False while the mouse is for something else, such as moving the camera
        bool Enabled = true;

        // The step of the tool, in units, degrees or scale
        std::optional<float> Snap;
    };

    // Handles on the world matrix in the area of the window. The handles point along the positive axes. An orthographic
    // projection is told by a 1 in its last row. Returns true while a handle is dragged, with the matrix changed.
    bool Gizmo(const Rect& area, const glm::mat4& view, const glm::mat4& projection, glm::mat4& world, const GizmoOptions& options);

    // The mouse is over a handle of the gizmo
    [[nodiscard]] bool IsOverGizmo();
}
