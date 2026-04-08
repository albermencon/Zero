#include "pch.h"
#include "VulkanDebug.h"
#include <Engine/Log.h>

namespace Zero::Debug
{
    VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT type,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void* userData)
    {
        const char* typeStr = "";
        if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
            typeStr = "Validation";
        else if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
            typeStr = "Performance";
        else if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)
            typeStr = "General";

        switch (severity)
        {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            ENGINE_CORE_ERROR("[{0} Error] [ {1} ] {2}",
                typeStr, data->pMessageIdName ? data->pMessageIdName : "?", data->pMessage);
            break;

        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            ENGINE_CORE_WARN("[{0} Warning] [ {1} ] {2}",
                typeStr, data->pMessageIdName ? data->pMessageIdName : "?", data->pMessage);
            break;

        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            ENGINE_CORE_INFO("[{0}] {1}", typeStr, data->pMessage);
            break;

        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            ENGINE_CORE_TRACE("[{0}] {1}", typeStr, data->pMessage);
            break;

        default:
            ENGINE_CORE_WARN("[Vulkan] {0}", data->pMessage);
            break;
        }

        if (data->objectCount > 0 && severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        {
            for (uint32_t i = 0; i < data->objectCount; ++i)
            {
                const auto& obj = data->pObjects[i];
                if (obj.pObjectName)
                    ENGINE_CORE_WARN("  Object {0}: [{1}] {2}",
                        i, vk::to_string(static_cast<vk::ObjectType>(obj.objectType)), obj.pObjectName);
            }
        }

        return VK_FALSE;
    }

    void SetObjectName(VkDevice device, uint64_t objectHandle, VkObjectType objectType, const char* name)
    {
#ifdef ENGINE_DEBUG
        if (!name) return;

        VkDebugUtilsObjectNameInfoEXT info{};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        info.objectType = objectType;
        info.objectHandle = objectHandle;
        info.pObjectName = name;

        static auto fn = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT"));
        if (fn) fn(device, &info);
#endif
    }

    void SetObjectName(const vk::raii::Device& device, uint64_t objectHandle, vk::ObjectType objectType, const char* name)
    {
#ifdef ENGINE_DEBUG
        SetObjectName(*device, objectHandle, static_cast<VkObjectType>(objectType), name);
#endif
    }

    void BeginLabel(VkCommandBuffer cmd, const char* name, float r, float g, float b, float a)
    {
#ifdef ENGINE_DEBUG
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name;
        label.color[0] = r; label.color[1] = g; label.color[2] = b; label.color[3] = a;

        static auto fn = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(nullptr, "vkCmdBeginDebugUtilsLabelEXT"));
        if (fn) fn(cmd, &label);
#endif
    }

    void EndLabel(VkCommandBuffer cmd)
    {
#ifdef ENGINE_DEBUG
        static auto fn = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(nullptr, "vkCmdEndDebugUtilsLabelEXT"));
        if (fn) fn(cmd);
#endif
    }

    void InsertLabel(VkCommandBuffer cmd, const char* name, float r, float g, float b, float a)
    {
#ifdef ENGINE_DEBUG
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name;
        label.color[0] = r; label.color[1] = g; label.color[2] = b; label.color[3] = a;

        static auto fn = reinterpret_cast<PFN_vkCmdInsertDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(nullptr, "vkCmdInsertDebugUtilsLabelEXT"));
        if (fn) fn(cmd, &label);
#endif
    }

    void BeginLabel(VkQueue queue, const char* name, float r, float g, float b, float a)
    {
#ifdef ENGINE_DEBUG
        VkDebugUtilsLabelEXT label{};
        label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name;
        label.color[0] = r; label.color[1] = g; label.color[2] = b; label.color[3] = a;

        static auto fn = reinterpret_cast<PFN_vkQueueBeginDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(nullptr, "vkQueueBeginDebugUtilsLabelEXT"));
        if (fn) fn(queue, &label);
#endif
    }

    void EndLabel(VkQueue queue)
    {
#ifdef ENGINE_DEBUG
        static auto fn = reinterpret_cast<PFN_vkQueueEndDebugUtilsLabelEXT>(
            vkGetInstanceProcAddr(nullptr, "vkQueueEndDebugUtilsLabelEXT"));
        if (fn) fn(queue);
#endif
    }
}
