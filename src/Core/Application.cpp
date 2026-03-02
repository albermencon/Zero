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

#include "Graphics/BackendFactory/GraphicsBackendFactory.h"
#include <Engine/Profiler/Profiler.h>

namespace VoxelEngine
{
    Application* Application::s_Instance = nullptr;

    Application::Application()
    {
        ENGINE_PROFILE_FUNCTION();
        ENGINE_CORE_ASSERT(!s_Instance, "Application already exists!");
        s_Instance = this;

        {
            ENGINE_PROFILE_SCOPE("Log Init");
            VoxelEngine::Log::Init();
        }

        // Initialize the layer stack
        {
            ENGINE_PROFILE_SCOPE("LayerStack Creation");
            m_LayerStack = std::make_unique<LayerStack>();
        }

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

        {
            ENGINE_PROFILE_SCOPE("Window Creation");
            m_Window = std::make_unique<VoxelEngine::Window>(props);
            m_Window->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));
        }

        {
            ENGINE_PROFILE_SCOPE("Backend Creation");
            m_backend = GraphicsBackend::Create(backend, *m_Window);
        }
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
        ENGINE_PROFILE_FUNCTION();
        ENGINE_PROFILE_THREAD("Main Thread");

        ENGINE_CORE_TRACE("Application started.");

        Time::Init();

        // Main loop
        while (m_Running)
        {
            ENGINE_PROFILE_FRAME();               // mark frame boundary
            ENGINE_PROFILE_SCOPE("Frame");

            Time::Update();

            // Poll OS events
            {
                ENGINE_PROFILE_SCOPE("PollEvents");
                m_Window->PollEvents();
            }

            {
                ENGINE_PROFILE_SCOPE("BeginFrame");
                m_backend->BeginFrame();
            }

            float dt = Time::GetDeltaTime();

            ENGINE_PROFILE_PLOT("DeltaTime", dt);
            ENGINE_PROFILE_PLOT("FPS", 1.0f / dt);
            ENGINE_PROFILE_PLOT("LayerCount", static_cast<float>(m_LayerStack->Size()));

            {
                ENGINE_PROFILE_SCOPE("Layer Update");

                for (auto& layer : *m_LayerStack)
                {
                    ENGINE_PROFILE_SCOPE_DYNAMIC(
                        layer->GetName().c_str(),
                        layer->GetName().size()
                    );

                    layer->OnUpdate(dt);
                }
            }

            {
                ENGINE_PROFILE_SCOPE("Layer Render");

                for (auto& layer : *m_LayerStack)
                {
                    ENGINE_PROFILE_SCOPE_DYNAMIC(
                        layer->GetName().c_str(),
                        layer->GetName().size()
                    );

                    layer->OnRender();
                }
            }

            {
                ENGINE_PROFILE_SCOPE("EndFrame");
                m_backend->EndFrame();
            }

            {
                ENGINE_PROFILE_SCOPE("SwapBuffers");
                m_backend->SwapBuffers();
            }

            {
                ENGINE_PROFILE_SCOPE("Input Update");
                Input::UpdateInput();
            }
        }

        // For Vulkan wait for the devices to finished
        {
            ENGINE_PROFILE_SCOPE("Backend Shutdown Wait");
            m_backend->OnFinished();
        }
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
        m_backend->OnRenderSurfaceResize();
        return true; // the layers don't need to see this event
    }

}
