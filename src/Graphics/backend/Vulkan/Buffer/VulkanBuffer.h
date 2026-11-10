#pragma once
#include "Graphics/core/Buffer.h"
#include <vk_mem_alloc.h>

namespace Zero 
{
    class VulkanMemory;

    class VulkanBuffer : public Buffer
    {
    public:
        VulkanBuffer(VulkanMemory* allocator, VkBuffer buffer, VmaAllocation allocation, size_t size, BufferUsage usage, MemoryDomain domain);
        virtual ~VulkanBuffer() override;

        virtual void* Map() override;
        virtual void  Unmap() override;

        virtual size_t GetSize() const override { return m_size; }
        virtual BufferUsage GetUsage() const override { return m_usage; }
        virtual MemoryDomain GetMemoryDomain() const override { return m_domain; }
        VmaAllocation GetAllocation() const { return m_allocation; }

        virtual bool IsMappable() const override;
        virtual uint64_t GetDeviceAddress() const override { return m_deviceAddress; }
        void SetDeviceAddress(uint64_t address) { m_deviceAddress = address; }

        VkBuffer GetHandle() const { return m_buffer; }

    private:
        VulkanMemory* m_allocator = nullptr;
        VkBuffer m_buffer = VK_NULL_HANDLE;
        VmaAllocation m_allocation = VK_NULL_HANDLE;
        size_t m_size = 0;
        BufferUsage m_usage;
        MemoryDomain m_domain;
        uint64_t m_deviceAddress = 0;
    };
}
