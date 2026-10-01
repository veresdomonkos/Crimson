#pragma once

#include <vulkan/vulkan.h>
#include "crimson/core/window.hpp"
#include "crimson/graphics/graphics_device.hpp"

namespace crimson::vulkan
{
    class VulkanDevice : public GraphicsDevice
    {
    public:
        explicit VulkanDevice(const Window& window);
        ~VulkanDevice();

        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;
        VulkanDevice(VulkanDevice&&) = delete;
        VulkanDevice& operator=(VulkanDevice&&) = delete;

        [[nodiscard]] VkInstance GetInstance() const { return m_instance; }
        [[nodiscard]] VkSurfaceKHR GetSurface() const { return m_surface; }
        [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }
        [[nodiscard]] VkDevice GetDevice() const { return m_device; }

        [[nodiscard]] VkQueue GetGraphicsQueue() const { return m_graphicsQueue; }
        [[nodiscard]] VkQueue GetPresentQueue() const { return m_presentQueue; }

        [[nodiscard]] uint32_t GetGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
        [[nodiscard]] uint32_t GetPresentQueueFamily() const { return m_presentQueueFamily; }

        [[nodiscard]] const VkPhysicalDeviceProperties& GetProperties() const { return m_properties; }
        [[nodiscard]] const VkPhysicalDeviceFeatures& GetFeatures() const { return m_features; }
        [[nodiscard]] const VkPhysicalDeviceMemoryProperties& GetMemoryProperties() const { return m_memoryProperties; }

        [[nodiscard]] uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

        void ImmediateSubmit(const std::function<void(VkCommandBuffer)>& function);
        void WaitIdle() const;
        inline static constexpr const char* s_validationLayer = "VK_LAYER_KHRONOS_validation";
    private:
        void CreateInstance();
        void SetupDebugMessenger();
        void CreateSurface(const Window& window);
        void PickPhysicalDevice();
        void CreateLogicalDevice();
        void CreateImmediateCommandPool();

        [[nodiscard]] bool IsDeviceSuitable(VkPhysicalDevice device) const;
        [[nodiscard]] uint32_t FindGraphicsQueueFamily(VkPhysicalDevice device) const;
        [[nodiscard]] uint32_t FindPresentQueueFamily(VkPhysicalDevice device) const;
        [[nodiscard]] bool CheckDeviceExtensionSupport(VkPhysicalDevice device) const;

        static void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

        static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            VkDebugUtilsMessageTypeFlagsEXT type,
            const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
            void* userData);

    private:
        VkInstance m_instance{};
        VkDebugUtilsMessengerEXT m_debugMessenger{};
        VkSurfaceKHR m_surface{};

        VkPhysicalDevice m_physicalDevice{};
        VkDevice m_device{};

        VkQueue m_graphicsQueue{};
        VkQueue m_presentQueue{};

        uint32_t m_graphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;
        uint32_t m_presentQueueFamily = VK_QUEUE_FAMILY_IGNORED;

        VkPhysicalDeviceProperties m_properties{};
        VkPhysicalDeviceFeatures m_features{};
        VkPhysicalDeviceMemoryProperties m_memoryProperties{};

        VkCommandPool m_immediateCommandPool{};
        VkCommandBuffer m_immediateCommandBuffer{};
        VkFence m_immediateFence{};

        bool m_validationEnabled = false;
    };
}
