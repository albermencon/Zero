#pragma once
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif
#include <Engine/Log.h>

namespace Zero::Debug
{
    static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
        vk::DebugUtilsMessageTypeFlagsEXT type,
        const vk::DebugUtilsMessengerCallbackDataEXT* data,
        void*)
    {
        switch (severity)
        {
        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eError:
            ENGINE_CORE_ERROR("[{}][{}] {}",
                vk::to_string(severity),
                vk::to_string(type),
                data->pMessage);
            break;

        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning:
            ENGINE_CORE_WARN("[{}][{}] {}",
                vk::to_string(severity),
                vk::to_string(type),
                data->pMessage);
            break;

        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo:
            ENGINE_CORE_INFO("[{}][{}] {}",
                vk::to_string(severity),
                vk::to_string(type),
                data->pMessage);
            break;

        case vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose:
        default:
            ENGINE_CORE_TRACE("[{}][{}] {}",
                vk::to_string(severity),
                vk::to_string(type),
                data->pMessage);
            break;
        }

        return VK_FALSE;
    }
}
