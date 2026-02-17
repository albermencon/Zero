#include "pch.h"
#include <Engine/Layer.h>
#include <Engine/Application.h>
#include <Engine/ApplicationEvent.h>
#include <Engine/Window.h>
#include <Engine/EventDispatcher.h>
#include <Engine/Log.h>
#include <Engine/Time.h>
#include "Platform/WindowImpl.h"
#include "Layers/LayerStack.h"
#include "Platform/InputInternal.h"

namespace VoxelEngine
{
    Application *Application::s_Instance = nullptr;

    Application::Application()
    {
        ENGINE_CORE_ASSERT(!s_Instance, "Application already exists!");
        s_Instance = this;

        VoxelEngine::Log::Init();

        // Initialize the layer stack
        m_LayerStack = std::make_unique<LayerStack>();

        // TODO: Replace this with a CLI, NativeWindow request factory maker
        // Create a window
        std::string Title = "Voxel Engine";
        uint32_t Width = 1280;
        uint32_t Height = 720;
        bool Fullscreen = false;
        bool VSync = true;
        bool Resizable = true;
        bool Visible = true;
        WindowProps props(Title, Width, Height, Fullscreen, VSync, Resizable, Visible);

        m_Window = std::make_unique<VoxelEngine::Window>(props);
        m_Window->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));
    }

    Application::~Application()
    {
    }

    void Application::PushLayer(std::unique_ptr<Layer> layer)
    {
        Layer *raw = m_LayerStack->PushLayer(std::move(layer));
        raw->OnAttach();
    }

    void Application::PushOverlay(std::unique_ptr<Layer> overlay)
    {
        Layer *raw = m_LayerStack->PushOverlay(std::move(overlay));
        raw->OnAttach();
    }

    void Application::Run()
    {
        ENGINE_CORE_TRACE("Application started.");

        Time::Init();

        // Main loop
        while (m_Running)
        {
            Time::Update();

            m_Window->OnUpdate();

            float dt = Time::GetDeltaTime();
            for (auto &layer : *m_LayerStack)
            {
                layer->OnUpdate(dt);
            }

            for (auto &layer : *m_LayerStack)
            {
                layer->OnRender();
            }

            Input::UpdateInput();
        }
    }

    void Application::OnEvent(Event &e)
    {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(Application::OnWindowClose));

        ENGINE_CORE_ASSERT(m_LayerStack, "LayerStack is null!");

        for (auto it = m_LayerStack->rbegin(); it != m_LayerStack->rend(); ++it)
        {
            if (e.Handled)
                break;
            (*it)->OnEvent(e);
        }
    }

    bool Application::OnWindowClose(WindowCloseEvent &e)
    {
        m_Running = false;
        return true;
    }

}
