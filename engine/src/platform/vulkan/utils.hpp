#pragma once

#include "vulkan_include.hpp"
#include "crimson/renderer/texture.hpp"

namespace crimson::vulkan::utils
{
    static constexpr vk::Format GetVkFormat(TextureFormat format)
    {
        switch (format)
        {
            case TextureFormat::RGBA8:           return vk::Format::eR8G8B8A8Unorm;
            case TextureFormat::RGB8:            return vk::Format::eR8G8B8Unorm;
            case TextureFormat::R8:              return vk::Format::eR8Unorm;
            case TextureFormat::RGBA16F:         return vk::Format::eR16G16B16A16Sfloat;
            case TextureFormat::RGBA32F:         return vk::Format::eR32G32B32A32Sfloat;
            case TextureFormat::Depth24Stencil8: return vk::Format::eD24UnormS8Uint;
            case TextureFormat::Depth32F:        return vk::Format::eD32Sfloat;
        }
        return vk::Format::eR8G8B8A8Unorm;
    }

    static constexpr bool HasStencilComponent(vk::Format format)
    {
        return format == vk::Format::eD24UnormS8Uint || format == vk::Format::eD32SfloatS8Uint;
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

    static constexpr vk::ImageAspectFlags GetImageAspect(TextureFormat format)
    {
        switch (format)
        {
            case TextureFormat::Depth24Stencil8:
                return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;

            case TextureFormat::Depth32F:
                return vk::ImageAspectFlagBits::eDepth;

            default:
                return vk::ImageAspectFlagBits::eColor;
        }
    }
}
