#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "crimson/renderer/shader_property_info.hpp"

namespace crimson
{
    // What an empty texture slot samples
    enum class ShaderTextureDefault
    {
        White,
        Normal // a flat normal map
    };

    // What a comment after a material property or texture declares, like Unity's Properties block:
    //   float Roughness; // @default 0.5 @range 0 1
    //   Texture2D NormalMap : register(t1, space1); // @default normal
    struct ShaderAnnotation
    {
        std::vector<float> Default;
        std::optional<std::pair<float, float>> Range;
        ShaderTextureDefault Texture = ShaderTextureDefault::White;
    };

    struct ShaderBinary
    {
        std::vector<uint32_t> Vertex;
        std::vector<uint32_t> Pixel;
        std::unordered_map<std::string, ShaderAnnotation> Annotations;
        std::vector<uint32_t> PixelDepth; // PSDepth, for the passes that only need depth. Empty if the shader has none.

        // The same stages before legalization, which drops the resources a stage never uses. Reflection reads these,
        // so that a texture the shader declares stays a material slot.
        std::vector<uint32_t> VertexInterface;
        std::vector<uint32_t> PixelInterface;
        std::vector<uint32_t> PixelDepthInterface;
    };

    struct ShaderMaterialTexture
    {
        uint32_t Binding = 0;
        bool Vertex = false;
        bool Pixel = false;
        ShaderTextureDefault Default = ShaderTextureDefault::White;
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

    namespace shader_bindings
    {
        // Resources are grouped by how often they change, each group is an HLSL space and a descriptor set.
        // assets/shaders/crimson/bindings.hlsli declares the same layout for the shaders.
        constexpr uint32_t kFrameSpace    = 0;
        constexpr uint32_t kMaterialSpace = 1;

        // HLSL numbers b, t and s registers separately but SPIR-V has one range, the compiler shifts them apart
        constexpr uint32_t kTextureShift = 16;
        constexpr uint32_t kSamplerShift = 32;
        constexpr uint32_t kStorageShift = 48;

        constexpr uint32_t kFrameBlock    = 0;
        constexpr uint32_t kLights        = kTextureShift + 0;
        constexpr uint32_t kShadows       = kTextureShift + 1;
        constexpr uint32_t kObjects       = kTextureShift + 2;
        constexpr uint32_t kShadowAtlas   = kTextureShift + 3;
        constexpr uint32_t kViews         = kTextureShift + 4;
        constexpr uint32_t kLinearWrap    = kSamplerShift + 0;
        constexpr uint32_t kLinearClamp   = kSamplerShift + 1;
        constexpr uint32_t kPointClamp    = kSamplerShift + 2;
        constexpr uint32_t kShadowCompare = kSamplerShift + 3;
    }

    // The per-draw push constant: where the draw's transform and its camera are in the frame's Objects and Views
    struct DrawData
    {
        uint32_t ObjectIndex = 0;
        uint32_t ViewIndex = 0;

        bool operator==(const DrawData&) const = default;
    };
}
