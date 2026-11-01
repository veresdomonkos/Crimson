#pragma once

namespace crimson
{
    enum class MaterialRenderMode
    {
        Opaque,
        Transparent // alpha blended, depth tested but not depth written, drawn after opaque
    };

    enum class CullMode
    {
        None,
        Back,
        Front
    };

    struct MaterialSettings
    {
        MaterialRenderMode RenderMode = MaterialRenderMode::Opaque;
        CullMode Cull = CullMode::Back;

        bool operator==(const MaterialSettings&) const = default;
    };
}
