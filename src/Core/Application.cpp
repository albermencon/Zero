#include "pch.h"
#include <Engine/Application.h>
#include <Engine/Layer.h>
#include <Engine/ApplicationEvent.h>
#include <Engine/Window.h>
#include <Engine/EventDispatcher.h>
#include <Engine/Log.h>
#include <Engine/Time.h>
#include <Engine/Scene/SceneManager.h>
#include "Layers/LayerStack.h"
#include "Platform/InputInternal.h"
#include "Graphics/Renderer/Renderer.h"
#include "JobSystem/BlockingThreadPool.h"

namespace Zero
{
    Application* Application::s_Instance = nullptr;

    Application& Application::Get()
    {
        return *s_Instance;
    }

    Application::Application()
    {
        ENGINE_CORE_ASSERT(!s_Instance, "Application already exists!");
        s_Instance = this;

        Zero::Log::Init();

        m_LayerStack = std::make_unique<LayerStack>();
        
        // TODO: Replace this with a CLI, NativeWindow request factory maker
        // Create a window
        std::string Title = "Voxel Engine";
        BackendType backend = BackendType::Vulkan;
        uint32_t Width = 1920;
        uint32_t Height = 1080;
        bool Fullscreen = false;
        bool VSync = true;
        bool Resizable = true;
        bool Visible = true;
        WindowProps props(Title, backend, Width, Height, Fullscreen, VSync, Resizable, Visible);

        m_Window = std::make_unique<Zero::Window>(props);
        m_Window->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));
        
        Renderer::Get().Init(RHI::API::Vulkan, m_Window.get());
        SceneManager::Get().Init();
        BlockingThreadPool::Get().Init(0); // default to hardware concurrency
    }

    Application::~Application()
    {
    }

    void Application::PushLayer(std::unique_ptr<Layer> layer)
    {
        Layer* raw = m_LayerStack->PushLayer(std::move(layer));
        raw->OnAttach();
    }

    void Application::PushOverlay(std::unique_ptr<Layer> overlay)
    {
        Layer* raw = m_LayerStack->PushOverlay(std::move(overlay));
        raw->OnAttach();
    }

    void Application::Run()
    {
        ENGINE_CORE_TRACE("Application started.");

        Time::Init();

        // Main loop
        uint64_t frameIndex = 0;
        while (m_Running)
        {
            Time::Update();
            m_Window->PollEvents(); // Poll OS events
            Input::UpdateInput();

            float dt = Time::GetDeltaTime();

            // Game logic
            for (auto& layer : *m_LayerStack)
            {
                layer->OnUpdate(dt);
            }

            SceneManager::Get().FlushCommands();

            // Build frame
            Renderer::Get().RequestFrame(); // must add a timeout

            FrameData* frame = SceneManager::Get().BuildFrame(frameIndex, dt);

            Renderer::Get().SubmitFrame(frame);
            frameIndex++;
        }

        Renderer::Get().Shutdown();
        SceneManager::Get().Shutdown();
        BlockingThreadPool::Get().Shutdown();
    }

    void Application::OnEvent(Event& e)
    {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(Application::OnWindowClose));
        dispatcher.Dispatch<RenderSurfaceResize>(BIND_EVENT_FN(Application::OnRenderSurfaceResize));

        ENGINE_CORE_ASSERT(m_LayerStack, "LayerStack is null!");

        for (auto it = m_LayerStack->rbegin(); it != m_LayerStack->rend(); ++it)
        {
            if (e.Handled)
                break;
            (*it)->OnEvent(e);
        }
    }

    bool Application::OnWindowClose(WindowCloseEvent& e)
    {
        m_Running = false;
        return true;
    }

    bool Application::OnRenderSurfaceResize(RenderSurfaceResize& e)
    {
        Renderer::Get().OnRenderSurfaceResize();
        return true; // the layers don't need to see this event
    }

}
