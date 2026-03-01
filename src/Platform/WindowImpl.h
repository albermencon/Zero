#pragma once
#include <Engine/Window.h>

namespace VoxelEngine
{
    class WindowImpl
    {
    public:
        virtual ~WindowImpl() = default;

        virtual void OnUpdate() = 0;

        virtual void SetEventCallback(const Window::EventCallbackFn &callback) = 0;
        virtual void SetVSync(bool enabled) = 0;
        virtual bool IsVSync() const = 0;

        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;

        virtual void SetWidth(uint32_t width) = 0;
        virtual void SetHeight(uint32_t height) = 0;
        virtual void SetSize(uint32_t width, uint32_t height) = 0;

        virtual void SetTitle(const std::string &title) = 0;
        virtual const std::string &GetTitle() const = 0;

        virtual void SetFullscreen(bool fullscreen) = 0;
        virtual bool IsFullscreen() const = 0;

        virtual void SetVisible(bool visible) = 0;
        virtual bool IsVisible() const = 0;

        virtual void *GetNativeWindow() const = 0;
        virtual BackendType GetBackend() const = 0;

        // Factory method
        static std::unique_ptr<WindowImpl> Create(const WindowProps &props);
    };
}
