#pragma once
#include "Platform/WindowImpl.h"

struct GLFWwindow;

namespace VoxelEngine
{
    class GLFWWindowImpl : public WindowImpl
    {
    public:
        GLFWWindowImpl(const WindowProps &props);
        virtual ~GLFWWindowImpl();

        virtual void OnUpdate() override;

        virtual void SetEventCallback(const Window::EventCallbackFn &callback) override;
        virtual void SetVSync(bool enabled) override;
        virtual bool IsVSync() const override;

        virtual uint32_t GetWidth() const override;
        virtual uint32_t GetHeight() const override;

        virtual void SetWidth(uint32_t width) override;
        virtual void SetHeight(uint32_t height) override;
        virtual void SetSize(uint32_t width, uint32_t height) override;

        virtual void SetTitle(const std::string &title) override;
        virtual const std::string &GetTitle() const override;

        virtual void SetFullscreen(bool fullscreen) override;
        virtual bool IsFullscreen() const override;

        virtual void SetVisible(bool visible) override;
        virtual bool IsVisible() const override;

        virtual void *GetNativeWindow() const override;

    private:
        void Init(const WindowProps &props);
        void Shutdown();

        // Callback registration functions
        void SetupCallBacks();

    private:
        GLFWwindow *m_Window;

        struct WindowData
        {
            std::string Title;
            uint32_t Width;
            uint32_t Height;
            bool VSync;
            bool Fullscreen;
            bool Visible;
            Window::EventCallbackFn EventCallback;
        };

        WindowData m_Data;
    };
}
