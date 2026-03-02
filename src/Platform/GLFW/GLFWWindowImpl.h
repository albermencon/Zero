#pragma once
#include "Platform/WindowImpl.h"

struct GLFWwindow;

namespace VoxelEngine
{
    class GLFWWindowImpl : public WindowImpl
    {
    public:
        GLFWWindowImpl(const WindowProps& props);
        virtual ~GLFWWindowImpl();

        virtual void PollEvents() override;

        virtual void SetEventCallback(const Window::EventCallbackFn& callback) override;
        virtual void SetVSync(bool enabled) override;
        virtual bool IsVSync() const override;

        // Window props
        virtual uint32_t GetWindowWidth() const override;
        virtual uint32_t GetWindowHeight() const override;

        virtual void SetWindowWidth(uint32_t width) override;
        virtual void SetWindowHeight(uint32_t height) override;
        virtual void SetWindowSize(uint32_t width, uint32_t height) override;

        // Frame buffer props
        virtual uint32_t GetFrameBufferWidth() const override;
        virtual uint32_t GetFrameBufferHeight() const override;

        // Other window props
        virtual void SetTitle(const std::string& title) override;
        virtual const std::string& GetTitle() const override;

        virtual void SetFullscreen(bool fullscreen) override;
        virtual bool IsFullscreen() const override;

        virtual void SetVisible(bool visible) override;
        virtual bool IsVisible() const override;

        virtual void* GetNativeWindow() const override;
        virtual BackendType GetBackend() const override;

    private:
        void Init(const WindowProps& props);
        void Shutdown();

        // Callback registration functions
        void SetupCallBacks();

    private:
        GLFWwindow* m_Window;
        BackendType m_backend;

        struct WindowState
        {
            std::string Title;
            uint32_t WindowWidth = 0;
            uint32_t WindowHeight = 0;

            uint32_t FramebufferWidth = 0;
            uint32_t FramebufferHeight = 0;

            bool VSync = false;
            bool Fullscreen = false;
            bool Visible = false;
            Window::EventCallbackFn EventCallback;
        };

        WindowState m_Data;
    };
}
