#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

#include "crimson/renderer/buffers.hpp"
#include "crimson/renderer/texture.hpp"

namespace crimson
{
    // The rules for updating what a buffer or a texture holds, which both backends follow and report with the same words.
    // Each returns the reason it can't be done, to log as an error, or nothing when it can.

    [[nodiscard]] inline std::optional<std::string_view> ValidateBufferUpdate(BufferUsage usage, std::size_t bufferSize, std::size_t dataSize)
    {
        if (usage == BufferUsage::Static)
            return "A static buffer can't be updated";

        if (dataSize > bufferSize)
            return "The data doesn't fit the buffer";

        return std::nullopt;
    }

    [[nodiscard]] constexpr bool IsDepthFormat(TextureFormat format)
    {
        return format == TextureFormat::Depth24Stencil8 || format == TextureFormat::Depth32F;
    }

    // Only a color texture that shaders sample can be updated, and only its one mip level
    [[nodiscard]] inline std::optional<std::string_view> ValidateTextureUpdate(const TextureInfo& texture, const TextureRegion& region)
    {
        if (!HasTextureUsage(texture.Usage, TextureUsage::Sampled) || IsDepthFormat(texture.Format))
            return "Only a color texture that shaders sample can be updated";

        if (texture.MipLevels > 1)
            return "A texture with mip levels can't be updated";

        if (static_cast<uint64_t>(region.X) + region.Width > texture.Width || static_cast<uint64_t>(region.Y) + region.Height > texture.Height)
            return "The region to update is outside the texture";

        return std::nullopt;
    }

    [[nodiscard]] inline bool CanUpdateTexture(const TextureInfo& texture)
    {
        return !ValidateTextureUpdate(texture, TextureRegion{}).has_value();
    }
}
