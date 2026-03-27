#include "VulkanDebug.h"
#include <Engine/Log.h>

namespace Zero::Debug
{
    VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT *data, void *userData)
    {
        const char* severityStr = "UNKNOWN";

        switch (severity)
        {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:   severityStr = "ERROR"; break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: severityStr = "WARN"; break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:    severityStr = "INFO"; break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: severityStr = "VERBOSE"; break;
        }

        ENGINE_CORE_ERROR("[{}] {}", severityStr, data->pMessage);
        return VK_FALSE;
    }
}
