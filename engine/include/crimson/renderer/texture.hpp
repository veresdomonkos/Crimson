#pragma once
#include <optional>
#include <vector>

#include "glm/fwd.hpp"

namespace crimson
{
    enum class TextureFormat
    {
        RGBA8,
        RGB8,
        R8,
        RGBA16F,
        RGBA32F,
        Depth24Stencil8,
        Depth32F,
    };

    enum class TextureUsage : glm::uint32_t
    {
        Sampled                = 1 << 0,
        ColorAttachment        = 1 << 1,
        DepthStencilAttachment = 1 << 2,
    };

    inline TextureUsage operator|(TextureUsage a, TextureUsage b)
    {
        return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline TextureUsage operator&(TextureUsage a, TextureUsage b)
    {
        return static_cast<TextureUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    inline bool HasTextureUsage(TextureUsage usage, TextureUsage flag)
    {
        return (usage & flag) != TextureUsage{};
    }

    struct TextureInfo
    {
        uint32_t Width  = 0;
        uint32_t Height = 0;
        TextureFormat Format = TextureFormat::RGBA8;
        TextureUsage Usage   = TextureUsage::Sampled;
        uint32_t MipLevels   = 1;
    };

    struct RenderTargetInfo
    {
        uint32_t Width  = 0;
        uint32_t Height = 0;
        std::vector<TextureFormat> ColorFormats;
        std::optional<TextureFormat> DepthFormat;
    };
}
