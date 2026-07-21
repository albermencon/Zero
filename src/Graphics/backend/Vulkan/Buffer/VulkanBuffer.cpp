#include "pch.h"
#include "VulkanBuffer.h"
#include "Graphics/backend/Vulkan/VMA/VulkanMemoryAllocator.h"
#include <Engine/Log.h>

namespace Zero 
{
    VulkanBuffer::VulkanBuffer(VulkanMemory* allocator, VkBuffer buffer, VmaAllocation allocation, size_t size, BufferUsage usage, MemoryDomain domain)
        : m_allocator(allocator)
        , m_buffer(buffer)
        , m_allocation(allocation)
        , m_size(size)
        , m_usage(usage)
        , m_domain(domain)
    {
    }

    VulkanBuffer::~VulkanBuffer()
    {
        if (m_buffer != VK_NULL_HANDLE && m_allocator && m_allocator->GetAllocator() != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(m_allocator->GetAllocator(), m_buffer, m_allocation);
        }
    }

    void* VulkanBuffer::Map()
    {
        if (!IsMappable())
        {
            ZERO_CORE_WARN("Attempting to map unmappable buffer");
            return nullptr;
        }

        void* mappedData = nullptr;
        VkResult result = vmaMapMemory(m_allocator->GetAllocator(), m_allocation, &mappedData);
        if (result != VK_SUCCESS)
        {
            ZERO_CORE_ERROR("Failed to map Vulkan buffer memory");
            return nullptr;
        }

        return mappedData;
    }

    void VulkanBuffer::Unmap()
    {
        if (IsMappable())
        {
            vmaUnmapMemory(m_allocator->GetAllocator(), m_allocation);
        }
    }

    bool VulkanBuffer::IsMappable() const
    {
        return m_domain != MemoryDomain::GPU;
    }
}
