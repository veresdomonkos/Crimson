#include "vulkan_imgui_backend.hpp"
#include <imgui/backends/imgui_impl_glfw.h>
#include <imgui/backends/imgui_impl_vulkan.h>

#include "vulkan_renderer.hpp"

namespace crimson::vulkan
{
    void VulkanImGuiBackend::Init(const Renderer& renderer, const Window& window)
    {
        const auto& vulkanRenderer = static_cast<const VulkanRenderer &>(renderer);
        const auto& vulkanResourceManager = static_cast<const VulkanResourceManager &>(renderer.GetResourceManager());

        auto* glfwWindow = static_cast<GLFWwindow*>(window.GetNativeHandle());
        ImGui_ImplGlfw_InitForVulkan(glfwWindow, true);

        RenderSurfaceHandle mainSurface(0,1);
        RenderTargetHandle rtHandle = vulkanResourceManager.GetCurrentBackBuffer(mainSurface);
        VulkanRenderTarget rt = vulkanResourceManager.GetRenderTarget(rtHandle);
        VkFormat colorFormat = vulkanResourceManager.GetTexture(vulkanResourceManager.GetColorAttachment(rtHandle, 0)).Format;

        VkPipelineRenderingCreateInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        renderingInfo.colorAttachmentCount = 1;
        renderingInfo.pColorAttachmentFormats = &colorFormat;

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.Instance = vulkanRenderer.GetDevice().GetInstance();
        initInfo.PhysicalDevice = vulkanRenderer.GetDevice().GetPhysicalDevice();
        initInfo.Device = vulkanRenderer.GetDevice().GetDevice();
        initInfo.QueueFamily = vulkanRenderer.GetDevice().GetGraphicsQueueFamilyIdx();
        initInfo.Queue = vulkanRenderer.GetDevice().GetGraphicsQueue();
        initInfo.DescriptorPool = VK_NULL_HANDLE;
        initInfo.DescriptorPoolSize = 1000;
        initInfo.MinImageCount = 2;
        initInfo.ImageCount = VulkanRenderer::MAX_FRAMES_IN_FLIGHT;
        initInfo.UseDynamicRendering = true;
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = renderingInfo;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        ImGui_ImplVulkan_Init(&initInfo);

        m_device = vulkanRenderer.GetDevice().GetDevice();
    }

    void VulkanImGuiBackend::NewFrame()
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
    }

    void VulkanImGuiBackend::RenderDrawData(ImDrawData* drawData, const NativeFrameHandles& handles)
    {
        ImGui_ImplVulkan_RenderDrawData(drawData, static_cast<VkCommandBuffer>(handles.CommandBuffer));
    }

    void VulkanImGuiBackend::Shutdown()
    {
        vkDeviceWaitIdle(m_device);

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

    ImTextureID VulkanImGuiBackend::GetOrCreateTextureId(ResourceManager& resourceManager, TextureHandle texture)
    {
        if (auto it = m_textureIdCache.find(texture); it != m_textureIdCache.end())
            return reinterpret_cast<ImTextureID>(it->second);

        auto& vkResMgr = static_cast<VulkanResourceManager&>(resourceManager);
        const VulkanTexture& tex = vkResMgr.GetTexture(texture);

        VkDescriptorSet descriptorSet = ImGui_ImplVulkan_AddTexture(
            m_sampler,
            tex.View,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );

        m_textureIdCache[texture] = descriptorSet;
        return reinterpret_cast<ImTextureID>(descriptorSet);
    }
}
