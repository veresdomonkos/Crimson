#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace crimson::vulkan
{
    enum class BufferMemory
    {
        DeviceLocal,
        HostMapped // persistently mapped and coherent, writes need no flush
    };

    class AllocatedBuffer
    {
    public:
        AllocatedBuffer() = default;
        AllocatedBuffer(VmaAllocator allocator, VkBuffer buffer, VmaAllocation allocation, void* mapped);
        ~AllocatedBuffer();

        AllocatedBuffer(const AllocatedBuffer&) = delete;
        AllocatedBuffer& operator=(const AllocatedBuffer&) = delete;
        AllocatedBuffer(AllocatedBuffer&& other) noexcept;
        AllocatedBuffer& operator=(AllocatedBuffer&& other) noexcept;

        [[nodiscard]] VkBuffer Get() const { return m_buffer; }
        [[nodiscard]] void* Mapped() const { return m_mapped; }
        explicit operator bool() const { return m_buffer != VK_NULL_HANDLE; }

    private:
        void Reset();

        VmaAllocator m_allocator = VK_NULL_HANDLE;
        VkBuffer m_buffer = VK_NULL_HANDLE;
        VmaAllocation m_allocation = VK_NULL_HANDLE;
        void* m_mapped = nullptr;
    };

    class AllocatedImage
    {
    public:
        AllocatedImage() = default;
        AllocatedImage(VmaAllocator allocator, VkImage image, VmaAllocation allocation);
        ~AllocatedImage();

        AllocatedImage(const AllocatedImage&) = delete;
        AllocatedImage& operator=(const AllocatedImage&) = delete;
        AllocatedImage(AllocatedImage&& other) noexcept;
        AllocatedImage& operator=(AllocatedImage&& other) noexcept;

        [[nodiscard]] VkImage Get() const { return m_image; }
        explicit operator bool() const { return m_image != VK_NULL_HANDLE; }

    private:
        void Reset();

        VmaAllocator m_allocator = VK_NULL_HANDLE;
        VkImage m_image = VK_NULL_HANDLE;
        VmaAllocation m_allocation = VK_NULL_HANDLE;
    };

    // Both return an empty object on failure
    [[nodiscard]] AllocatedBuffer CreateAllocatedBuffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, BufferMemory memory);
    [[nodiscard]] AllocatedImage CreateAllocatedImage(VmaAllocator allocator, const VkImageCreateInfo& info, bool dedicated);
}
