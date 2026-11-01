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

        bool CheckValidationLayerSupport()
        {
            uint32_t layerCount = 0;
            vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

            std::vector<VkLayerProperties> availableLayers(layerCount);
            vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

            for (const auto& layer : availableLayers)
            {
                if (std::strcmp(layer.layerName, VulkanDevice::s_validationLayer) == 0)
                    return true;
            }

            return false;
        }

        VkResult CreateDebugUtilsMessenger(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* createInfo, VkDebugUtilsMessengerEXT* debugMessenger)
        {
            const auto function = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));

            if (!function)
                return VK_ERROR_EXTENSION_NOT_PRESENT;

            return function(instance, createInfo, nullptr, debugMessenger);
        }

        void DestroyDebugUtilsMessenger(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger)
        {
            const auto function = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));

            if (function)
                function(instance, debugMessenger, nullptr);
        }
    }

    VulkanDevice::VulkanDevice(const Window& window)
        : m_primaryWindow(window)
    {
        m_validationEnabled = EnableValidation && CheckValidationLayerSupport();

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
        if (m_device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(m_device);

            if (m_immediateFence != VK_NULL_HANDLE)
                vkDestroyFence(m_device, m_immediateFence, nullptr);

            if (m_immediateCommandPool != VK_NULL_HANDLE)
                vkDestroyCommandPool(m_device, m_immediateCommandPool, nullptr);

            if (m_allocator != VK_NULL_HANDLE)
                vmaDestroyAllocator(m_allocator);

            vkDestroyDevice(m_device, nullptr);
            m_device = VK_NULL_HANDLE;
        }

        if (m_surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
            m_surface = VK_NULL_HANDLE;
        }

        if (m_debugMessenger != VK_NULL_HANDLE)
        {
            DestroyDebugUtilsMessenger(m_instance, m_debugMessenger);
            m_debugMessenger = VK_NULL_HANDLE;
        }

        if (m_instance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
        }
    }

    void VulkanDevice::CreateInstance()
    {
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
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

        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

        std::vector<const char*> layers;
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};

        const VkValidationFeatureEnableEXT enabledFeature = VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        VkValidationFeaturesEXT validationFeatures{};
        validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        validationFeatures.enabledValidationFeatureCount = 1;
        validationFeatures.pEnabledValidationFeatures = &enabledFeature;

        if (m_validationEnabled)
        {
            layers.push_back(s_validationLayer);

            createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
            createInfo.ppEnabledLayerNames = layers.data();

            PopulateDebugMessengerCreateInfo(debugCreateInfo);
            createInfo.pNext = &debugCreateInfo;

            if (syncValidation)
                debugCreateInfo.pNext = &validationFeatures;
        }

        if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan instance.");
    }

    void VulkanDevice::SetupDebugMessenger()
    {
        if (!m_validationEnabled)
            return;

        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        PopulateDebugMessengerCreateInfo(createInfo);

        if (CreateDebugUtilsMessenger(m_instance, &createInfo, &m_debugMessenger) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan debug messenger.");
    }

    void VulkanDevice::CreateSurface(const Window& window)
    {
        if (glfwCreateWindowSurface(
                m_instance,
                static_cast<GLFWwindow*>(window.GetNativeHandle()),
                nullptr,
                &m_surface) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create Vulkan surface.");
        }
    }

    void VulkanDevice::PickPhysicalDevice()
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

        if (deviceCount == 0)
            throw std::runtime_error("No Vulkan-capable GPU found.");

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

        for (VkPhysicalDevice device : devices)
        {
            if (IsDeviceSuitable(device))
            {
                m_physicalDevice = device;
                break;
            }
        }

        if (m_physicalDevice == VK_NULL_HANDLE)
            throw std::runtime_error("Failed to find a suitable Vulkan GPU.");

        m_graphicsQueueFamily = FindGraphicsQueueFamily(m_physicalDevice);

        vkGetPhysicalDeviceProperties(m_physicalDevice, &m_properties);
        vkGetPhysicalDeviceFeatures(m_physicalDevice, &m_features);

        LOG_INFO("[Vulkan Device] Selected GPU: {}", m_properties.deviceName);
    }

    void VulkanDevice::CreateLogicalDevice()
    {
        float queuePriority = 1.0f;

        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = m_graphicsQueueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        VkPhysicalDeviceFeatures deviceFeatures{};
        deviceFeatures.samplerAnisotropy = m_features.samplerAnisotropy;

        VkPhysicalDeviceVulkan13Features vulkan13Features{};
        vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        vulkan13Features.dynamicRendering = VK_TRUE;
        vulkan13Features.synchronization2 = VK_TRUE;

        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = 1;
        createInfo.pQueueCreateInfos = &queueCreateInfo;
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(DeviceExtensions.size());
        createInfo.ppEnabledExtensionNames = DeviceExtensions.data();
        createInfo.pNext = &vulkan13Features;

        if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan logical device.");

        vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
    }

    void VulkanDevice::CreateImmediateCommandPool()
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = m_graphicsQueueFamily;

        if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_immediateCommandPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create immediate command pool.");

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = m_immediateCommandPool;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(m_device, &allocInfo, &m_immediateCommandBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to allocate immediate command buffer.");

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

        if (vkCreateFence(m_device, &fenceInfo, nullptr, &m_immediateFence) != VK_SUCCESS)
            throw std::runtime_error("Failed to create immediate fence.");
    }

    void VulkanDevice::CreateAllocator()
    {
        VmaAllocatorCreateInfo createInfo{};
        createInfo.instance = m_instance;
        createInfo.physicalDevice = m_physicalDevice;
        createInfo.device = m_device;
        createInfo.vulkanApiVersion = VK_API_VERSION_1_3;

        if (vmaCreateAllocator(&createInfo, &m_allocator) != VK_SUCCESS)
            throw std::runtime_error("Failed to create Vulkan memory allocator.");
    }

    bool VulkanDevice::IsDeviceSuitable(VkPhysicalDevice device) const
    {
        if (FindGraphicsQueueFamily(device) == VK_QUEUE_FAMILY_IGNORED)
            return false;

        if (!CheckDeviceExtensionSupport(device))
            return false;

        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceFeatures(device, &features);

        if (!features.samplerAnisotropy)
            return false;

        VkPhysicalDeviceVulkan13Features f13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        VkPhysicalDeviceFeatures2 f2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f13 };
        vkGetPhysicalDeviceFeatures2(device, &f2);

        if (!f13.dynamicRendering || !f13.synchronization2)
            return false;

        return true;
    }

    uint32_t VulkanDevice::FindGraphicsQueueFamily(VkPhysicalDevice device) const
    {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        // One family has to do both: swapchain images are exclusive and never change queue family
        for (uint32_t i = 0; i < queueFamilyCount; ++i)
        {
            if (queueFamilies[i].queueCount == 0 || !(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
                continue;

            VkBool32 presentSupport = VK_FALSE;

            if (vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport) != VK_SUCCESS)
                continue;

            if (presentSupport)
                return i;
        }

        return VK_QUEUE_FAMILY_IGNORED;
    }

    bool VulkanDevice::CheckDeviceExtensionSupport(VkPhysicalDevice device) const
    {
        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

        for (const char* requiredExtension : DeviceExtensions)
        {
            bool found = false;

            for (const auto& extension : availableExtensions)
            {
                if (std::strcmp(requiredExtension, extension.extensionName) == 0)
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
        if (m_device != VK_NULL_HANDLE)
            vkDeviceWaitIdle(m_device);
    }

    void VulkanDevice::PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
    {
        createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = DebugCallback;
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL VulkanDevice::DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData)
    {
        if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            LOG_ERROR("[Vulkan Device] {}", callbackData->pMessage);
        else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            LOG_WARN("[Vulkan Device] {}", callbackData->pMessage);

        return VK_FALSE;
    }

    void VulkanDevice::ImmediateSubmit(const std::function<void(VkCommandBuffer)>& function)
    {
        if (m_immediateCommandPool == VK_NULL_HANDLE ||
            m_immediateCommandBuffer == VK_NULL_HANDLE ||
            m_immediateFence == VK_NULL_HANDLE)
        {
            throw std::runtime_error("Immediate submission resources are not initialized.");
        }

        if (vkResetFences(m_device, 1, &m_immediateFence) != VK_SUCCESS)
            throw std::runtime_error("Failed to reset immediate fence.");

        if (vkResetCommandBuffer(m_immediateCommandBuffer, 0) != VK_SUCCESS)
            throw std::runtime_error("Failed to reset immediate command buffer.");

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (vkBeginCommandBuffer(m_immediateCommandBuffer, &beginInfo) != VK_SUCCESS)
            throw std::runtime_error("Failed to begin immediate command buffer.");

        function(m_immediateCommandBuffer);

        if (vkEndCommandBuffer(m_immediateCommandBuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to end immediate command buffer.");

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &m_immediateCommandBuffer;

        if (vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_immediateFence) != VK_SUCCESS)
            throw std::runtime_error("Failed to submit immediate command buffer.");

        if (vkWaitForFences(m_device, 1, &m_immediateFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
            throw std::runtime_error("Failed to wait for immediate command buffer.");
    }
}