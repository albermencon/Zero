#pragma once
#include <memory>
#include <string>
#include <functional>
#include "Core.h"
#include "Event.h"

namespace VoxelEngine
{
    struct WindowProps
    {
        std::string Title;
        uint32_t Width;
        uint32_t Height;
        bool Fullscreen;
        bool VSync;
        bool Resizable;
        bool Visible;

        WindowProps(const std::string &title = "Voxel Engine",
                    uint32_t width = 1280,
                    uint32_t height = 720,
                    bool fullscreen = false,
                    bool vsync = true,
                    bool resizable = true,
                    bool visible = true)
            : Title(title), Width(width), Height(height),
              Fullscreen(fullscreen), VSync(vsync),
              Resizable(resizable), Visible(visible) {}
    };

    // Forward declare platform-specific window implementation
    class WindowImpl;

    class ENGINE_API Window
    {
    public:
        using EventCallbackFn = std::function<void(Event &)>;

        Window(const WindowProps &props = WindowProps());
        ~Window();

        // No copying allowed
        Window(const Window &) = delete;
        Window &operator=(const Window &) = delete;

        // Allow moving
        Window(Window &&other) noexcept;
        Window &operator=(Window &&other) noexcept;

        // Core functionality
        void OnUpdate();
        void SetEventCallback(const EventCallbackFn& callback);
        void SetVSync(bool enabled);
        bool IsVSync() const;

        // Window properties
        uint32_t GetWidth() const;
        uint32_t GetHeight() const;
        std::pair<uint32_t, uint32_t> GetSize() const;

        void SetWidth(uint32_t width);
        void SetHeight(uint32_t height);
        void SetSize(uint32_t width, uint32_t height);

        // Window state
        void SetTitle(const std::string &title);
        const std::string &GetTitle() const;

        void SetFullscreen(bool fullscreen);
        bool IsFullscreen() const;

        void SetVisible(bool visible);
        bool IsVisible() const;

        void *GetNativeWindow() const;

        // Platform info
        static std::string GetPlatformName();

    private:
        std::unique_ptr<WindowImpl> m_Impl;
    };
}
