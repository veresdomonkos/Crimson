#include "vulkan_imgui_backend.hpp"
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_vulkan.h>

namespace crimson::vulkan
{
    VulkanImGuiBackend::VulkanImGuiBackend(VulkanDevice& device, VulkanResourceManager& resourceManager, const Window& window)
        : m_device(device), m_resourceManager(resourceManager)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        auto* glfwWindow = static_cast<GLFWwindow*>(window.GetNativeHandle());
        ImGui_ImplGlfw_InitForVulkan(glfwWindow, true);

        RenderTargetHandle rtHandle = resourceManager.GetCurrentBackBuffer();
        VkFormat colorFormat = static_cast<VkFormat>(resourceManager.GetTexture(resourceManager.GetColorAttachment(rtHandle, 0)).Format);

        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.Instance = static_cast<VkInstance>(*device.GetInstance());
        initInfo.PhysicalDevice = static_cast<VkPhysicalDevice>(*device.GetPhysicalDevice());
        initInfo.Device = static_cast<VkDevice>(*device.GetDevice());
        initInfo.QueueFamily = device.GetGraphicsQueueFamily();
        initInfo.Queue = static_cast<VkQueue>(*device.GetGraphicsQueue());
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
            static_cast<VkImageView>(*tex.View),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );

        m_textureIdCache[texture] = descriptorSet;
        return reinterpret_cast<ImTextureID>(descriptorSet);
    }

    void VulkanImGuiBackend::ReleaseTexture(TextureHandle texture)
    {
        const auto it = m_textureIdCache.find(texture);
        if (it == m_textureIdCache.end())
            return;

        // Frames still in flight can be using the set
        m_device.WaitIdle();

        ImGui_ImplVulkan_RemoveTexture(it->second);
        m_textureIdCache.erase(it);
    }
}
