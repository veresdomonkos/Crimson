#pragma once

#include <vulkan/vulkan.h>
#include "crimson/renderer/texture.hpp"

namespace crimson::vulkan::utils
{
    static constexpr VkFormat GetVkFormat(TextureFormat format)
    {
        switch (format)
        {
            case TextureFormat::RGBA8:           return VK_FORMAT_R8G8B8A8_UNORM;
            case TextureFormat::RGB8:            return VK_FORMAT_R8G8B8_UNORM;
            case TextureFormat::R8:              return VK_FORMAT_R8_UNORM;
            case TextureFormat::RGBA16F:         return VK_FORMAT_R16G16B16A16_SFLOAT;
            case TextureFormat::RGBA32F:         return VK_FORMAT_R32G32B32A32_SFLOAT;
            case TextureFormat::Depth24Stencil8: return VK_FORMAT_D24_UNORM_S8_UINT;
            case TextureFormat::Depth32F:        return VK_FORMAT_D32_SFLOAT;
        }
        return VK_FORMAT_R8G8B8A8_UNORM;
    }

    static constexpr bool HasStencilComponent(VkFormat format)
    {
        return format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
    }

    static constexpr bool HasUsage(TextureUsage usage, TextureUsage flag)
    {
        return (static_cast<uint32_t>(usage) & static_cast<uint32_t>(flag)) != 0;
    }

    static constexpr uint32_t GetBytesPerPixel(TextureFormat format)
    {
        switch (format)
        {
            case TextureFormat::R8:      return 1;
            case TextureFormat::RGB8:    return 3;
            case TextureFormat::RGBA8:   return 4;
            case TextureFormat::RGBA16F: return 8;
            case TextureFormat::RGBA32F: return 16;
            default:                     return 4;
        }
    }
}
