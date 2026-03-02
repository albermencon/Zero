#include "pch.h"
#include <Engine/Window.h>
#include "Platform/WindowImpl.h"

namespace VoxelEngine
{
    Window::Window(const WindowProps& props)
        : m_Impl(WindowImpl::Create(props))
    {
    }

    Window::~Window() = default;

    Window::Window(Window&& other) noexcept = default;
    Window& Window::operator=(Window&& other) noexcept = default;

    void Window::PollEvents()
    {
        m_Impl->PollEvents();
    }

    void Window::SetEventCallback(const EventCallbackFn& callback)
    {
        m_Impl->SetEventCallback(callback);
    }

    void Window::SetVSync(bool enabled)
    {
        m_Impl->SetVSync(enabled);
    }

    bool Window::IsVSync() const
    {
        return m_Impl->IsVSync();
    }

    uint32_t Window::GetWindowWidth() const
    {
        return m_Impl->GetWindowWidth();
    }

    uint32_t Window::GetWindowHeight() const
    {
        return m_Impl->GetWindowHeight();
    }

    std::pair<uint32_t, uint32_t> Window::GetWindowSize() const
    {
        return { GetWindowWidth(), GetWindowHeight() };
    }

    void Window::SetWindowWidth(uint32_t width)
    {
        m_Impl->SetWindowWidth(width);
    }

    uint32_t Window::GetFrameBufferWidth() const
    {
        return m_Impl->GetFrameBufferWidth();
    }

    uint32_t Window::GetFrameBufferHeight() const
    {
        return m_Impl->GetFrameBufferWidth();
    }

    std::pair<uint32_t, uint32_t> Window::GetFrameBufferSize() const
    {
        return { GetFrameBufferWidth(), GetFrameBufferHeight() };
    }

    void Window::SetTitle(const std::string& title)
    {
        m_Impl->SetTitle(title);
    }

    const std::string& Window::GetTitle() const
    {
        return m_Impl->GetTitle();
    }

    void Window::SetFullscreen(bool fullscreen)
    {
        m_Impl->SetFullscreen(fullscreen);
    }

    bool Window::IsFullscreen() const
    {
        return m_Impl->IsFullscreen();
    }

    void Window::SetVisible(bool visible)
    {
        m_Impl->SetVisible(visible);
    }

    bool Window::IsVisible() const
    {
        return m_Impl->IsVisible();
    }

    void* Window::GetNativeWindow() const
    {
        return m_Impl->GetNativeWindow();
    }

    std::string Window::GetPlatformName()
    {
#ifdef PLATFORM_WINDOWS
        return "Windows";
#elif defined(PLATFORM_LINUX)
        return "Linux";
#elif defined(PLATFORM_MACOS)
        return "macOS";
#elif defined(PLATFORM_ANDROID)
        return "Android";
#elif defined(PLATFORM_IOS)
        return "iOS";
#else
        return "Unknown";
#endif
    }
}
