#include <vulkan/vulkan.h>
#include "crimson/ui/imgui_backend.hpp"

namespace crimson::vulkan
{
    class VulkanImGuiBackend : public ImGuiBackend
    {
    public:
        void Init(const Renderer& handles, const Window& window) override;
        void NewFrame() override;
        void RenderDrawData(ImDrawData* drawData, const NativeFrameHandles& handles) override;
        void Shutdown() override;
        ImTextureID GetOrCreateTextureId(ResourceManager& resourceManager, TextureHandle texture) override;
    private:
        VkDevice m_device;
        VkSampler m_sampler = VK_NULL_HANDLE;
        std::unordered_map<TextureHandle, VkDescriptorSet, HandleHash<TextureTag>> m_textureIdCache;
    };
}