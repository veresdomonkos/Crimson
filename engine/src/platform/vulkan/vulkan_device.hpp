#pragma once

#include <functional>

#include "vulkan_include.hpp"
#include "vulkan_allocator.hpp"
#include "crimson/core/window.hpp"
#include "crimson/graphics/graphics_device.hpp"

namespace crimson::vulkan
{
    class VulkanDevice : public GraphicsDevice
    {
    public:
        explicit VulkanDevice(const Window& window);
        ~VulkanDevice() override;

        VulkanDevice(const VulkanDevice&) = delete;
        VulkanDevice& operator=(const VulkanDevice&) = delete;
        VulkanDevice(VulkanDevice&&) = delete;
        VulkanDevice& operator=(VulkanDevice&&) = delete;

        [[nodiscard]] const Window& GetPrimaryWindow() const { return m_primaryWindow; }

        [[nodiscard]] const vk::raii::Instance& GetInstance() const { return m_instance; }
        [[nodiscard]] vk::SurfaceKHR GetSurface() const { return *m_surface; }
        [[nodiscard]] const vk::raii::PhysicalDevice& GetPhysicalDevice() const { return m_physicalDevice; }
        [[nodiscard]] const vk::raii::Device& GetDevice() const { return m_device; }

        [[nodiscard]] const vk::raii::Queue& GetGraphicsQueue() const { return m_graphicsQueue; }
        [[nodiscard]] uint32_t GetGraphicsQueueFamily() const { return m_graphicsQueueFamily; }

        [[nodiscard]] const vk::PhysicalDeviceProperties& GetProperties() const { return m_properties; }
        [[nodiscard]] const vk::PhysicalDeviceFeatures& GetFeatures() const { return m_features; }

        [[nodiscard]] VmaAllocator GetAllocator() const { return m_allocator; }

        void ImmediateSubmit(const std::function<void(vk::CommandBuffer)>& function);
        void WaitIdle() const override;
        inline static constexpr const char* s_validationLayer = "VK_LAYER_KHRONOS_validation";
    private:
        void CreateInstance();
        void SetupDebugMessenger();
        void CreateSurface(const Window& window);
        void PickPhysicalDevice();
        void CreateLogicalDevice();
        void CreateImmediateCommandPool();
        void CreateAllocator();

        [[nodiscard]] bool IsDeviceSuitable(const vk::raii::PhysicalDevice& device) const;
        [[nodiscard]] uint32_t FindGraphicsQueueFamily(const vk::raii::PhysicalDevice& device) const;
        [[nodiscard]] bool CheckDeviceExtensionSupport(const vk::raii::PhysicalDevice& device) const;

        static vk::DebugUtilsMessengerCreateInfoEXT MakeDebugMessengerCreateInfo();

        static VKAPI_ATTR vk::Bool32 VKAPI_CALL DebugCallback(
            vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
            vk::DebugUtilsMessageTypeFlagsEXT type,
            const vk::DebugUtilsMessengerCallbackDataEXT* callbackData,
            void* userData);

    private:
        const Window& m_primaryWindow;

        // Declared in creation order, destruction runs in reverse
        vk::raii::Context m_context;
        vk::raii::Instance m_instance{ nullptr };
        vk::raii::DebugUtilsMessengerEXT m_debugMessenger{ nullptr };
        vk::raii::SurfaceKHR m_surface{ nullptr };

        vk::raii::PhysicalDevice m_physicalDevice{ nullptr };
        vk::raii::Device m_device{ nullptr };

        vk::raii::Queue m_graphicsQueue{ nullptr };
        uint32_t m_graphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;

        vk::PhysicalDeviceProperties m_properties{};
        vk::PhysicalDeviceFeatures m_features{};

        VmaAllocator m_allocator{};

        vk::raii::CommandPool m_immediateCommandPool{ nullptr };
        vk::raii::CommandBuffer m_immediateCommandBuffer{ nullptr };
        vk::raii::Fence m_immediateFence{ nullptr };

        bool m_validationEnabled = false;
    };
}
