#define VMA_IMPLEMENTATION
#include "vulkan_allocator.hpp"

#include <utility>

namespace crimson::vulkan
{
    AllocatedBuffer::AllocatedBuffer(VmaAllocator allocator, VkBuffer buffer, VmaAllocation allocation, void* mapped)
        : m_allocator(allocator), m_buffer(buffer), m_allocation(allocation), m_mapped(mapped)
    {
    }

    AllocatedBuffer::~AllocatedBuffer()
    {
        Reset();
    }

    AllocatedBuffer::AllocatedBuffer(AllocatedBuffer&& other) noexcept
        : m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE)),
          m_buffer(std::exchange(other.m_buffer, VK_NULL_HANDLE)),
          m_allocation(std::exchange(other.m_allocation, VK_NULL_HANDLE)),
          m_mapped(std::exchange(other.m_mapped, nullptr))
    {
    }

    AllocatedBuffer& AllocatedBuffer::operator=(AllocatedBuffer&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
            m_buffer = std::exchange(other.m_buffer, VK_NULL_HANDLE);
            m_allocation = std::exchange(other.m_allocation, VK_NULL_HANDLE);
            m_mapped = std::exchange(other.m_mapped, nullptr);
        }

        return *this;
    }

    void AllocatedBuffer::Reset()
    {
        if (m_buffer != VK_NULL_HANDLE)
            vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);

        m_buffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
        m_mapped = nullptr;
    }

    AllocatedImage::AllocatedImage(VmaAllocator allocator, VkImage image, VmaAllocation allocation)
        : m_allocator(allocator), m_image(image), m_allocation(allocation)
    {
    }

    AllocatedImage::~AllocatedImage()
    {
        Reset();
    }

    AllocatedImage::AllocatedImage(AllocatedImage&& other) noexcept
        : m_allocator(std::exchange(other.m_allocator, VK_NULL_HANDLE)),
          m_image(std::exchange(other.m_image, VK_NULL_HANDLE)),
          m_allocation(std::exchange(other.m_allocation, VK_NULL_HANDLE))
    {
    }

    AllocatedImage& AllocatedImage::operator=(AllocatedImage&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_allocator = std::exchange(other.m_allocator, VK_NULL_HANDLE);
            m_image = std::exchange(other.m_image, VK_NULL_HANDLE);
            m_allocation = std::exchange(other.m_allocation, VK_NULL_HANDLE);
        }

        return *this;
    }

    void AllocatedImage::Reset()
    {
        if (m_image != VK_NULL_HANDLE)
            vmaDestroyImage(m_allocator, m_image, m_allocation);

        m_image = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }

    AllocatedBuffer CreateAllocatedBuffer(VmaAllocator allocator, vk::DeviceSize size, vk::BufferUsageFlags usage, BufferMemory memory)
    {
        vk::BufferCreateInfo createInfo{};
        createInfo.size = size;
        createInfo.usage = usage;
        createInfo.sharingMode = vk::SharingMode::eExclusive;

        const VkBufferCreateInfo& bufferInfo = createInfo;

        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (memory == BufferMemory::HostMapped)
        {
            allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            allocationInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        }

        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VmaAllocationInfo info{};

        if (vmaCreateBuffer(allocator, &bufferInfo, &allocationInfo, &buffer, &allocation, &info) != VK_SUCCESS)
            return {};

        return { allocator, buffer, allocation, info.pMappedData };
    }

    AllocatedImage CreateAllocatedImage(VmaAllocator allocator, const vk::ImageCreateInfo& createInfo, bool dedicated)
    {
        const VkImageCreateInfo& info = createInfo;

        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (dedicated)
            allocationInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;

        if (vmaCreateImage(allocator, &info, &allocationInfo, &image, &allocation, nullptr) != VK_SUCCESS)
            return {};

        return { allocator, image, allocation };
    }
}
