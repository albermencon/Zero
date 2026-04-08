#pragma once
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

namespace Zero::Debug
{
    VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void* userData);

    void SetObjectName(VkDevice device, uint64_t objectHandle, VkObjectType objectType, const char* name);
    void SetObjectName(const vk::raii::Device& device, uint64_t objectHandle, vk::ObjectType objectType, const char* name);

    void BeginLabel(VkCommandBuffer cmd, const char* name, float r = 1.f, float g = 1.f, float b = 1.f, float a = 1.f);
    void EndLabel(VkCommandBuffer cmd);
    void InsertLabel(VkCommandBuffer cmd, const char* name, float r = 1.f, float g = 1.f, float b = 1.f, float a = 1.f);

    void BeginLabel(VkQueue queue, const char* name, float r = 1.f, float g = 1.f, float b = 1.f, float a = 1.f);
    void EndLabel(VkQueue queue);
}
