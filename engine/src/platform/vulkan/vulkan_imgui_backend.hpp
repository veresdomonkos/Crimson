#include <vulkan/vulkan.h>

#include "vulkan_renderer.hpp"
#include "crimson/ui/imgui_backend.hpp"

namespace crimson::vulkan
{
    class VulkanImGuiBackend : public ImGuiBackend
    {
    public:
        VulkanImGuiBackend(VulkanDevice& device, VulkanResourceManager& resourceManager, const Window& window);
        ~VulkanImGuiBackend() override;
        void NewFrame() override;
        void RenderDrawData(ImDrawData* drawData, const NativeFrameHandles& handles) override;
        ImTextureID GetOrCreateTextureId(TextureHandle texture) override;
    private:
        VulkanDevice& m_device;
        VulkanResourceManager& m_resourceManager;
        VkSampler m_sampler = VK_NULL_HANDLE;
        std::unordered_map<TextureHandle, VkDescriptorSet, HandleHash<TextureTag>> m_textureIdCache;
    };
}