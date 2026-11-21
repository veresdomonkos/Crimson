#include "crimson_editor/ui/gizmo.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <imgui.h>
#include <ImGuizmo.h>

namespace crimson::editor::ui
{
    static ImGuizmo::OPERATION ToOperation(GizmoTool tool)
    {
        switch (tool)
        {
            case GizmoTool::Move:   return ImGuizmo::TRANSLATE;
            case GizmoTool::Rotate: return ImGuizmo::ROTATE;
            case GizmoTool::Scale:  return ImGuizmo::SCALE;
        }

        return ImGuizmo::TRANSLATE;
    }

    bool Gizmo(const Rect& area, const glm::mat4& view, const glm::mat4& projection, glm::mat4& world, const GizmoOptions& options)
    {
        ImGuizmo::Enable(options.Enabled);

        // The handles always point along the positive axes, as in Unity, rather than turning to face the camera
        ImGuizmo::AllowAxisFlip(false);

        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(area.Min.x, area.Min.y, area.Size().x, area.Size().y);

        // Only an orthographic projection has a 1 there
        ImGuizmo::SetOrthographic(projection[3][3] == 1.0f);

        const glm::vec3 snap(options.Snap.value_or(0.0f));

        ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), ToOperation(options.Tool),
            options.Space == GizmoSpace::Local ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
            glm::value_ptr(world), nullptr, options.Snap ? glm::value_ptr(snap) : nullptr);

        return ImGuizmo::IsUsing();
    }

    bool IsOverGizmo()
    {
        return ImGuizmo::IsOver();
    }
}
