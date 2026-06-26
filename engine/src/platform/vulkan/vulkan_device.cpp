#include "vulkan_device.hpp"

#include <vector>
#include "GLFW/glfw3.h"

namespace crimson::vulkan
{
    uint32_t VulkanDevice::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
    {
        VkPhysicalDeviceMemoryProperties memProperties;
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProperties);

        for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
        {
            if ((typeFilter & (1 << i)) &&
                (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }

        LOG_ERROR("CRIMSON_ERROR: Failed to find suitable memory type for allocation!");
    }

    void VulkanDevice::Init()
    {
        CreateInstance();
        SetupDebugMessenger();
        PickPhysicalDevice();
        CreateLogicalDevice();
    }

    void VulkanDevice::Shutdown() const
    {
        if (m_device != VK_NULL_HANDLE)
            vkDestroyDevice(m_device, nullptr);

#ifndef NDEBUG
        if (m_debugMessenger != VK_NULL_HANDLE)
        {
            if (const auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT")))
                fn(m_instance, m_debugMessenger, nullptr);
        }
#endif

        if (m_instance != VK_NULL_HANDLE)
            vkDestroyInstance(m_instance, nullptr);
    }

    void VulkanDevice::CreateInstance()
    {
        uint32_t version = 0;
        vkEnumerateInstanceVersion(&version);
        LOG_WARN("[Renderer]: Vulkan version:\t{}.{}.{}", VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version), VK_VERSION_PATCH(version));

        VkApplicationInfo appInfo{};
        appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName   = "Crimson";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName        = "Crimson Engine";
        appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion         = VK_API_VERSION_1_3;

        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

#ifndef NDEBUG
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> layers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, layers.data());

        for (const auto& layer : layers)
        {
            if (strcmp(layer.layerName, s_validationLayer) == 0)
            {
                m_validationAvailable = true;
                break;
            }
        }

        if (m_validationAvailable)
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        else
            LOG_WARN("[Renderer]: Vulkan validation layers not available");
#endif

        VkInstanceCreateInfo createInfo{};
        createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo        = &appInfo;
        createInfo.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();

#ifndef NDEBUG
        VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
        if (m_validationAvailable)
        {
            PopulateDebugMessengerCreateInfo(debugInfo);
            createInfo.enabledLayerCount   = 1;
            createInfo.ppEnabledLayerNames = &s_validationLayer;
            createInfo.pNext               = &debugInfo;
        }
#endif

        if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS)
        {
            LOG_ERROR("[Renderer]: Failed to create Vulkan instance!");
            return;
        }
    }

    void VulkanDevice::SetupDebugMessenger()
    {
#ifndef NDEBUG
        if (!m_validationAvailable)
            return;

        VkDebugUtilsMessengerCreateInfoEXT info{};
        PopulateDebugMessengerCreateInfo(info);

        if (const auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT")))
            fn(m_instance, &info, nullptr, &m_debugMessenger);
        else
            LOG_WARN("[Renderer]: Could not load vkCreateDebugUtilsMessengerEXT");
#endif
    }

    void VulkanDevice::PickPhysicalDevice()
    {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

        if (deviceCount == 0)
        {
            LOG_ERROR("[Renderer]: No Vulkan GPU found!");
            return;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());
        m_physicalDevice = devices[0];

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
        LOG_INFO("[Renderer]: Renderer:\t\t{}", props.deviceName);
    }

    void VulkanDevice::CreateLogicalDevice()
    {
        uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(
            m_physicalDevice,
            &queueCount,
            nullptr
        );

        std::vector<VkQueueFamilyProperties> queues(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(
            m_physicalDevice,
            &queueCount,
            queues.data()
        );

        for (uint32_t i = 0; i < queueCount; i++)
        {
            if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                m_graphicsQueueFamilyIdx = i;
                break;
            }
        }

        float priority = 1.0f;

        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = m_graphicsQueueFamilyIdx;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;


        // Vulkan 1.3 features
        VkPhysicalDeviceVulkan13Features vulkan13{};
        vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

        vulkan13.dynamicRendering = VK_TRUE;
        vulkan13.synchronization2 = VK_TRUE;


        const char* extensions[] =
        {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };


        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

        deviceInfo.pNext = &vulkan13;

        deviceInfo.queueCreateInfoCount = 1;
        deviceInfo.pQueueCreateInfos = &queueInfo;

        deviceInfo.enabledExtensionCount = 1;
        deviceInfo.ppEnabledExtensionNames = extensions;


        if (vkCreateDevice(
            m_physicalDevice,
            &deviceInfo,
            nullptr,
            &m_device) != VK_SUCCESS)
        {
            LOG_ERROR("[Renderer]: Failed to create logical device!");
            return;
        }

        vkGetDeviceQueue(
            m_device,
            m_graphicsQueueFamilyIdx,
            0,
            &m_graphicsQueue
        );
    }

    void VulkanDevice::PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &info)
    {
        info = {};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        info.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT    |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        info.pfnUserCallback = DebugCallback;
    }
}