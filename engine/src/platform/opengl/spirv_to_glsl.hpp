#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace crimson::opengl
{
    // OpenGL has no descriptor sets, each resource kind has one flat range of slots
    constexpr uint32_t kFrameBlockSlot    = 0;
    constexpr uint32_t kMaterialBlockSlot = 1;

    constexpr uint32_t kLightsSlot  = 0;
    constexpr uint32_t kShadowsSlot = 1;
    constexpr uint32_t kObjectsSlot = 2;
    constexpr uint32_t kViewsSlot   = 3;

    constexpr const char* kDrawUniformName = "CrimsonDraw";

    // OpenGL has no separate textures and samplers either, every pair a shader samples with takes a texture unit
    struct GlslTextureUnit
    {
        std::string Texture;
        std::string Sampler;
        uint32_t Unit = 0;
    };

    // Pass the same units for all stages of a program, so a pair used by two stages shares its unit.
    // Errors go to the log.
    [[nodiscard]] std::optional<std::string> TranslateSpirvToGlsl(std::span<const uint32_t> spirv, std::vector<GlslTextureUnit>& units);
}
