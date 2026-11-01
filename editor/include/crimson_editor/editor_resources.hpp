#pragma once
#include <crimson/renderer/buffer_layout.hpp>

#include "crimson/renderer/resource_handles.hpp"
#include "glm/vec3.hpp"

namespace crimson::editor
{
    struct Vertex
    {
        glm::vec3 Position;
        glm::vec3 Normal;
    };

    const BufferLayout kVertexLayout{ ShaderDataType::Float3, ShaderDataType::Float3 };
}
