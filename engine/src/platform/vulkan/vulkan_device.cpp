#include "vulkan_device.hpp"

#include <GLFW/glfw3.h>

#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "crimson/core/log.hpp"
#include "crimson/core/window.hpp"

namespace crimson::vulkan
{
    namespace
    {
        const std::vector<const char*> DeviceExtensions =
        {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

#ifndef NDEBUG
        constexpr bool EnableValidation = true;
#else
        constexpr bool EnableValidation = false;
#endif

        bool CheckValidationLayerSupport(const vk::raii::Context& context)
        {
            for (const auto& layer : context.enumerateInstanceLayerProperties())
            {
                if (std::strcmp(layer.layerName.data(), VulkanDevice::s_validationLayer) == 0)
                    return true;
            }

            return false;
        }
    }

    VulkanDevice::VulkanDevice(const Window& window)
        : m_primaryWindow(window)
    {
        m_validationEnabled = EnableValidation && CheckValidationLayerSupport(m_context);

        CreateInstance();
        SetupDebugMessenger();
        CreateSurface(window);
        PickPhysicalDevice();
        CreateLogicalDevice();
        CreateImmediateCommandPool();
        CreateAllocator();
    }

    VulkanDevice::~VulkanDevice()
    {
        WaitIdle();

        if (m_allocator != VK_NULL_HANDLE)
            vmaDestroyAllocator(m_allocator);
    }

    void VulkanDevice::CreateInstance()
    {
        vk::ApplicationInfo appInfo{};
        appInfo.pApplicationName = "Crimson";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "Crimson";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3;

        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

        if (!glfwExtensions)
            throw std::runtime_error("Failed to get GLFW Vulkan extensions.");

        std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

        const bool syncValidation = m_validationEnabled && std::getenv("CRIMSON_VK_SYNC_VALIDATION") != nullptr;

        if (m_validationEnabled)
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        if (syncValidation)
            extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);

        vk::InstanceCreateInfo createInfo{};
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        std::vector<const char*> layers;
        vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo = MakeDebugMessengerCreateInfo();

        const vk::ValidationFeatureEnableEXT enabledFeature = vk::ValidationFeatureEnableEXT::eSynchronizationValidation;
        vk::ValidationFeaturesEXT validationFeatures{};
        validationFeatures.enabledValidationFeatureCount = 1;
        validationFeatures.pEnabledValidationFeatures = &enabledFeature;

        if (m_validationEnabled)
        {
            layers.push_back(s_validationLayer);

            createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
            createInfo.ppEnabledLayerNames = layers.data();

            createInfo.pNext = &debugCreateInfo;

            if (syncValidation)
                debugCreateInfo.pNext = &validationFeatures;
        }

        m_instance = m_context.createInstance(createInfo);
    }

    void VulkanDevice::SetupDebugMessenger()
    {
        if (!m_validationEnabled)
            return;

        m_debugMessenger = m_instance.createDebugUtilsMessengerEXT(MakeDebugMessengerCreateInfo());
    }

    void VulkanDevice::CreateSurface(const Window& window)
    {
        VkSurfaceKHR surface = VK_NULL_HANDLE;

        if (glfwCreateWindowSurface(
                static_cast<VkInstance>(*m_instance),
                static_cast<GLFWwindow*>(window.GetNativeHandle()),
                nullptr,
                &surface) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create Vulkan surface.");
        }

        m_surface = vk::raii::SurfaceKHR(m_instance, surface);
    }

    void VulkanDevice::PickPhysicalDevice()
    {
        std::vector<vk::raii::PhysicalDevice> devices = m_instance.enumeratePhysicalDevices();

        if (devices.empty())
            throw std::runtime_error("No Vulkan-capable GPU found.");

        for (vk::raii::PhysicalDevice& device : devices)
        {
            if (IsDeviceSuitable(device))
            {
                m_physicalDevice = std::move(device);
                break;
            }
        }

        if (!*m_physicalDevice)
            throw std::runtime_error("Failed to find a suitable Vulkan GPU.");

        m_graphicsQueueFamily = FindGraphicsQueueFamily(m_physicalDevice);

        m_properties = m_physicalDevice.getProperties();
        m_features = m_physicalDevice.getFeatures();

        LOG_INFO("[Vulkan Device] Selected GPU: {}", m_properties.deviceName.data());
    }

    void VulkanDevice::CreateLogicalDevice()
    {
        float queuePriority = 1.0f;

        vk::DeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.queueFamilyIndex = m_graphicsQueueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        vk::PhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.samplerAnisotropy = m_features.samplerAnisotropy;

        vk::PhysicalDeviceVulkan13Features vulkan13Features{};
        vulkan13Features.dynamicRendering = VK_TRUE;
        vulkan13Features.synchronization2 = VK_TRUE;

        vk::DeviceCreateInfo createInfo{};
        createInfo.queueCreateInfoCount = 1;
        createInfo.pQueueCreateInfos = &queueCreateInfo;
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(DeviceExtensions.size());
        createInfo.ppEnabledExtensionNames = DeviceExtensions.data();
        createInfo.pNext = &vulkan13Features;

        m_device = m_physicalDevice.createDevice(createInfo);
        m_graphicsQueue = m_device.getQueue(m_graphicsQueueFamily, 0);
    }

    void VulkanDevice::CreateImmediateCommandPool()
    {
        vk::CommandPoolCreateInfo poolInfo{};
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        poolInfo.queueFamilyIndex = m_graphicsQueueFamily;

        m_immediateCommandPool = m_device.createCommandPool(poolInfo);

        vk::CommandBufferAllocateInfo allocInfo{};
        allocInfo.level = vk::CommandBufferLevel::ePrimary;
        allocInfo.commandPool = *m_immediateCommandPool;
        allocInfo.commandBufferCount = 1;

        m_immediateCommandBuffer = std::move(m_device.allocateCommandBuffers(allocInfo).front());
        m_immediateFence = m_device.createFence({});
    }

    void VulkanDevice::CreateAllocator()
    {
        VmaAllocatorCreateInfo createInfo{};
        createInfo.instance = static_cast<VkInstance>(*m_instance);
        createInfo.physicalDevice = static_cast<VkPhysicalDevice>(*m_physicalDevice);
        createInfo.device = static_cast<VkDevice>(*m_device);
        createInfo.vulkanApiVersion = VK_API_VERSION_1_3;

        if (vmaCreateAllocator(&createInfo, &m_allocator) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan memory allocator.");
    }

    bool VulkanDevice::IsDeviceSuitable(const vk::raii::PhysicalDevice& device) const
    {
        if (FindGraphicsQueueFamily(device) == VK_QUEUE_FAMILY_IGNORED)
            return false;

        if (!CheckDeviceExtensionSupport(device))
            return false;

        if (!device.getFeatures().samplerAnisotropy)
            return false;

        const auto features = device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features>();
        const auto& f13 = features.get<vk::PhysicalDeviceVulkan13Features>();

        if (!f13.dynamicRendering || !f13.synchronization2)
            return false;

        return true;
    }

    uint32_t VulkanDevice::FindGraphicsQueueFamily(const vk::raii::PhysicalDevice& device) const
    {
        const std::vector<vk::QueueFamilyProperties> queueFamilies = device.getQueueFamilyProperties();

        // One family has to do both: swapchain images are exclusive and never change queue family
        for (uint32_t i = 0; i < queueFamilies.size(); ++i)
        {
            if (queueFamilies[i].queueCount == 0 || !(queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics))
                continue;

            if (device.getSurfaceSupportKHR(i, *m_surface))
                return i;
        }

        return VK_QUEUE_FAMILY_IGNORED;
    }

    bool VulkanDevice::CheckDeviceExtensionSupport(const vk::raii::PhysicalDevice& device) const
    {
        const std::vector<vk::ExtensionProperties> availableExtensions = device.enumerateDeviceExtensionProperties();

        for (const char* requiredExtension : DeviceExtensions)
        {
            bool found = false;

            for (const auto& extension : availableExtensions)
            {
                if (std::strcmp(requiredExtension, extension.extensionName.data()) == 0)
                {
                    found = true;
                    break;
                }
            }

            if (!found)
                return false;
        }

        return true;
    }

    void VulkanDevice::WaitIdle() const
    {
        if (*m_device)
            m_device.waitIdle();
    }

    vk::DebugUtilsMessengerCreateInfoEXT VulkanDevice::MakeDebugMessengerCreateInfo()
    {
        vk::DebugUtilsMessengerCreateInfoEXT createInfo{};
        createInfo.messageSeverity =
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
            vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
        createInfo.messageType =
            vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
            vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
            vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
        createInfo.pfnUserCallback = DebugCallback;

        return createInfo;
    }

    VKAPI_ATTR vk::Bool32 VKAPI_CALL VulkanDevice::DebugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* callbackData, void* userData)
    {
        if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
            LOG_ERROR("[Vulkan Device] {}", callbackData->pMessage);
        else if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
            LOG_WARN("[Vulkan Device] {}", callbackData->pMessage);

        return VK_FALSE;
    }

    void VulkanDevice::ImmediateSubmit(const std::function<void(vk::CommandBuffer)>& function)
    {
        m_device.resetFences(*m_immediateFence);
        m_immediateCommandBuffer.reset();

        vk::CommandBufferBeginInfo beginInfo{};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

        m_immediateCommandBuffer.begin(beginInfo);
        function(*m_immediateCommandBuffer);
        m_immediateCommandBuffer.end();

        const vk::CommandBuffer commandBuffer = *m_immediateCommandBuffer;

        vk::SubmitInfo submitInfo{};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        m_graphicsQueue.submit(submitInfo, *m_immediateFence);

        if (m_device.waitForFences(*m_immediateFence, VK_TRUE, UINT64_MAX) != vk::Result::eSuccess)
            throw std::runtime_error("Failed to wait for immediate command buffer.");
    }
}
