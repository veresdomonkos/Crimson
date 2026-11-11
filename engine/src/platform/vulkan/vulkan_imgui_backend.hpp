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
        void RenderDrawData(ImDrawData* drawData, const NativeContext& context) override;
        ImTextureID GetOrCreateTextureId(TextureHandle texture) override;
        void ReleaseTexture(TextureHandle texture) override;
    private:
        VulkanDevice& m_device;
        VulkanResourceManager& m_resourceManager;
        std::unordered_map<TextureHandle, VkDescriptorSet, HandleHash<TextureTag>> m_textureIdCache;
    };
}