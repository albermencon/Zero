#pragma once
#include "Core.h"
#include <memory>

namespace Zero
{
    class Event;
    class LayerStack;
    class Layer;
    class Window;
    class WindowCloseEvent;
    class RenderSurfaceResize;
    class GraphicsDevice;

    class ENGINE_API Application
    {
    public:
        Application();
        virtual ~Application();

        void PushLayer(std::unique_ptr<Layer> layer);
        void PushOverlay(std::unique_ptr<Layer> overlay);

        inline static Application& Get();
        inline Window& GetWindow() { return *m_Window; }

        void Run();

    private:
        // Event handling
        void OnEvent(Event& e);
        bool OnWindowClose(WindowCloseEvent& e);
        bool OnRenderSurfaceResize(RenderSurfaceResize& e);

    private:
        std::unique_ptr<class Window> m_Window;
        std::unique_ptr<class GraphicsDevice> m_backend;
        std::unique_ptr<LayerStack> m_LayerStack;
        static Application* s_Instance;
        bool m_Running = true;
    };

    // To be defined in client
    Application* CreateApplication();
}
