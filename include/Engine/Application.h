#pragma once
#include "Core.h"
#include <memory>

namespace VoxelEngine
{
    class Event;
    class LayerStack;
    class Layer;
    class Window;
    class WindowCloseEvent;

    class ENGINE_API Application
    {
    public:
        Application();
        virtual ~Application();

        void PushLayer(std::unique_ptr<Layer> layer);
        void PushOverlay(std::unique_ptr<Layer> overlay);

        inline static Application &Get() { return *s_Instance; }
        inline Window &GetWindow() { return *m_Window; }

        void Run();

    private:
        // Event handling
        void OnEvent(Event &e);
        bool OnWindowClose(WindowCloseEvent &e);

    private:
        std::unique_ptr<class Window> m_Window;
        std::unique_ptr<LayerStack> m_LayerStack;
        static Application *s_Instance;
        bool m_Running = true;
    };

    // To be defined in client
    Application *CreateApplication();
}
