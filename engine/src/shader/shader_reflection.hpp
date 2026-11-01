#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>

#include "crimson/renderer/shader_interface.hpp"
#include "crimson/renderer/shader_property_info.hpp"

namespace crimson
{
    struct ShaderMaterialTexture
    {
        uint32_t Binding = 0;
        bool Vertex = false;
        bool Pixel = false;
    };

    // What a shader's material space contains
    struct ShaderInterface
    {
        std::size_t MaterialBlockSize = 0;
        uint32_t MaterialBlockBinding = 0;
        bool MaterialBlockInVertex = false;
        bool MaterialBlockInPixel = false;

        std::unordered_map<std::string, ShaderPropertyInfo> Properties;
        std::unordered_map<std::string, ShaderMaterialTexture> Textures;
    };

    // Also checks the frame and view spaces against the engine's structs, failing with a logged error on any mismatch
    [[nodiscard]] std::optional<ShaderInterface> ReflectShaderInterface(const ShaderBinary& binary);
}
