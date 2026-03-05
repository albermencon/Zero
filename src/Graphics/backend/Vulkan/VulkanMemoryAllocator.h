#pragma once
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif
#include <vk_mem_alloc.h>

namespace Zero 
{
    class VulkanMemory
    {
    public:
        void Initialize(VkInstance instance,
            VkPhysicalDevice physicalDevice,
            VkDevice device);

        void Shutdown();

        VmaAllocator GetAllocator() const { return m_Allocator; }

    private:
        VmaAllocator m_Allocator = VK_NULL_HANDLE;
    };
}
