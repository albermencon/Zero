#include "pch.h"
#include "Platform/WindowImpl.h"

#ifdef PLATFORM_WINDOWS
#include "Platform/GLFW/GLFWWindowImpl.h"
#elif defined(PLATFORM_LINUX)
#include "Platform/GLFW/GLFWWindowImpl.h"
#elif defined(PLATFORM_MACOS)
#include "Platform/GLFW/GLFWWindowImpl.h"
#elif defined(PLATFORM_ANDROID)
#include "Platform/Android/AndroidWindowImpl.h"
#elif defined(PLATFORM_IOS)
#include "Platform/iOS/iOSWindowImpl.h"
#endif

namespace VoxelEngine
{
    std::unique_ptr<WindowImpl> WindowImpl::Create(const WindowProps &props)
    {
#ifdef PLATFORM_WINDOWS
        return std::make_unique<GLFWWindowImpl>(props);
#elif defined(PLATFORM_LINUX)
        return std::make_unique<GLFWWindowImpl>(props);
#elif defined(PLATFORM_MACOS)
        return std::make_unique<GLFWWindowImpl>(props);
#elif defined(PLATFORM_ANDROID)
        // not supported yet
        return std::make_unique<AndroidWindowImpl>(props);
#elif defined(PLATFORM_IOS)
        // not supported yet
        return std::make_unique<iOSWindowImpl>(props);
#else
#error "Unsupported platform!"
#endif
    }
}
