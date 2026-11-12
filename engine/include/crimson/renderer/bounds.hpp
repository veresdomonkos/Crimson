#pragma once
#include <array>

#include <glm/glm.hpp>

#include "crimson/renderer/mesh.hpp"

namespace crimson
{
    // The box around the bounds once they are transformed. It holds all of them, but can be larger than they are.
    [[nodiscard]] inline MeshBounds TransformBounds(const MeshBounds& bounds, const glm::mat4& transform)
    {
        const glm::vec3 center = glm::vec3(transform * glm::vec4((bounds.Min + bounds.Max) * 0.5f, 1.0f));
        const glm::vec3 extent = (bounds.Max - bounds.Min) * 0.5f;

        const glm::mat3 axes(transform);
        const glm::vec3 reach = glm::abs(axes[0]) * extent.x + glm::abs(axes[1]) * extent.y + glm::abs(axes[2]) * extent.z;
        return { center - reach, center + reach };
    }

    // Whether a box in world space is entirely outside what a view sees, for a projection with a depth of 0 to 1, like
    // glm's _ZO ones. It errs towards inside: a box near a corner of the view can be outside and still count as in it.
    [[nodiscard]] inline bool IsOutsideView(const MeshBounds& bounds, const glm::mat4& viewProj)
    {
        const auto row = [&viewProj](int i) { return glm::vec4(viewProj[0][i], viewProj[1][i], viewProj[2][i], viewProj[3][i]); };

        // Left, right, bottom, top, near and far, each facing into the view
        const std::array<glm::vec4, 6> planes = { row(3) + row(0), row(3) - row(0), row(3) + row(1), row(3) - row(1), row(2), row(3) - row(2) };

        for (const glm::vec4& plane : planes)
        {
            // The corner farthest into the view, if even that one is behind the plane the whole box is
            const glm::vec3 normal(plane);
            const glm::vec3 corner = glm::mix(bounds.Min, bounds.Max, glm::step(glm::vec3(0.0f), normal));

            if (glm::dot(normal, corner) + plane.w < 0.0f)
                return true;
        }

        return false;
    }
}
