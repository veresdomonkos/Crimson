#pragma once

#include <vulkan/vulkan.h>
#include "crimson/core/log.hpp"

namespace crimson::vulkan
{
    class VulkanDevice
    {
    public:
        VulkanDevice() = default;

        // Non-copyable, non-movable
        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;
        VulkanDevice(VulkanDevice&&) = delete;
        VulkanDevice& operator=(VulkanDevice&&) = delete;

        [[nodiscard]] VkInstance GetInstance() const { return m_instance; }
        [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }
        [[nodiscard]] VkDevice GetDevice() const { return m_device; }
        [[nodiscard]] VkQueue GetGraphicsQueue() const { return m_graphicsQueue; }
        [[nodiscard]] uint32_t GetGraphicsQueueFamilyIdx() const { return m_graphicsQueueFamilyIdx; }

        void Init();
        void Shutdown() const;

        static VKAPI_ATTR VkBool32 VKAPI_CALL
        DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
                      const VkDebugUtilsMessengerCallbackDataEXT* data,void* userdata)
        {
            if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            {
                LOG_WARN("[Renderer]: Vulkan {}", data->pMessage);
            }

            return VK_FALSE;
        }

    private:
        void CreateInstance();
        void SetupDebugMessenger();
        void PickPhysicalDevice();
        void CreateLogicalDevice();

        static void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& info);

        VkInstance m_instance{};
        VkPhysicalDevice m_physicalDevice{};
        VkDevice m_device{};
        VkQueue m_graphicsQueue{};
        uint32_t m_graphicsQueueFamilyIdx = UINT32_MAX;
        VkDebugUtilsMessengerEXT m_debugMessenger{};
        bool m_validationAvailable = false;

        inline static const char* s_validationLayer = "VK_LAYER_KHRONOS_validation";
    };
}
