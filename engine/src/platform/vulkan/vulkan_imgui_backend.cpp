#include "vulkan_imgui_backend.hpp"
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_vulkan.h>

namespace crimson::vulkan
{
    VulkanImGuiBackend::VulkanImGuiBackend(VulkanDevice& device, VulkanResourceManager& resourceManager, const Window& window)
        : m_device(device), m_resourceManager(resourceManager)
    {
        auto* glfwWindow = static_cast<GLFWwindow*>(window.GetNativeHandle());
        ImGui_ImplGlfw_InitForVulkan(glfwWindow, true);

        RenderTargetHandle rtHandle = resourceManager.GetCurrentBackBuffer();
        VulkanRenderTarget rt = resourceManager.GetRenderTarget(rtHandle);
        VkFormat colorFormat = resourceManager.GetTexture(resourceManager.GetColorAttachment(rtHandle, 0)).Format;

        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.Instance = device.GetInstance();
        initInfo.PhysicalDevice = device.GetPhysicalDevice();
        initInfo.Device = device.GetDevice();
        initInfo.QueueFamily = device.GetGraphicsQueueFamily();
        initInfo.Queue = device.GetGraphicsQueue();
        initInfo.DescriptorPool = VK_NULL_HANDLE;
        initInfo.DescriptorPoolSize = 1000;
        initInfo.MinImageCount = 2;
        initInfo.ImageCount = MAX_FRAMES_IN_FLIGHT;
        initInfo.UseDynamicRendering = true;
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = renderingInfo;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        ImGui_ImplVulkan_Init(&initInfo);
    }

    VulkanImGuiBackend::~VulkanImGuiBackend()
    {
        m_device.WaitIdle();

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    void VulkanImGuiBackend::NewFrame()
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    void VulkanImGuiBackend::RenderDrawData(ImDrawData* drawData, const NativeFrameHandles& handles)
    {
        ImGui_ImplVulkan_RenderDrawData(drawData, static_cast<VkCommandBuffer>(handles.CommandBuffer));
    }

    ImTextureID VulkanImGuiBackend::GetOrCreateTextureId(TextureHandle texture)
    {
        if (auto it = m_textureIdCache.find(texture); it != m_textureIdCache.end())
            return reinterpret_cast<ImTextureID>(it->second);

        const VulkanTexture& tex = m_resourceManager.GetTexture(texture);

        VkDescriptorSet descriptorSet = ImGui_ImplVulkan_AddTexture(
            m_sampler,
            tex.View,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );

        m_textureIdCache[texture] = descriptorSet;
        return reinterpret_cast<ImTextureID>(descriptorSet);
    }
}
