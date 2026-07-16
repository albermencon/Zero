#include "pch.h"
#include <Engine/Application.h>
#include <Engine/Layer.h>
#include <Engine/ApplicationEvent.h>
#include <Engine/Window.h>
#include <Engine/EventDispatcher.h>
#include <Engine/Log.h>
#include <Engine/Time.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/ConfigSystem.h>
#include <Engine/File/FileWatcher.h>
#include "Layers/LayerStack.h"
#include "Engine/Layers/ImGuiLayer.h"
#include "Platform/InputInternal.h"
#include "Graphics/Renderer/Renderer.h"
#include "Graphics/ShaderCompiler/ShaderCompiler.h"
#include "JobSystem/BlockingThreadPool.h"
#include <Engine/Profiler/Profiler.h>
#include <cstdint>
#include <thread>
#include <chrono>

namespace Zero
{
    Application* Application::s_Instance = nullptr;

    Application& Application::Get()
    {
        return *s_Instance;
    }

    Application::Application()
    {
        ZERO_CORE_ASSERT(!s_Instance, "Application already exists!");
        s_Instance = this;

        Zero::Log::Init();

        m_LayerStack = std::make_unique<LayerStack>();
        
        if (!ConfigSystem::Get().Load("config.ini"))
        {
            ConfigSystem::Get().SetString("Window", "Title", "Zero");
            ConfigSystem::Get().SetUInt("Window", "Width", 1920);
            ConfigSystem::Get().SetUInt("Window", "Height", 1080);
            ConfigSystem::Get().SetBool("Window", "Fullscreen", false);
            ConfigSystem::Get().SetBool("Window", "VSync", true);
            ConfigSystem::Get().SetBool("Window", "Resizable", true);
            ConfigSystem::Get().SetBool("Window", "Visible", true);
            ConfigSystem::Get().SetUInt("Window", "Backend", static_cast<uint32_t>(BackendType::Vulkan));
            
            ConfigSystem::Get().SetUInt("Workers", "Number", 0);

            ConfigSystem::Get().Save("config.ini");
        }

        std::string Title = ConfigSystem::Get().GetString("Window", "Title", "Zero");
        uint32_t Width = ConfigSystem::Get().GetUInt("Window", "Width", 1920);
        uint32_t Height = ConfigSystem::Get().GetUInt("Window", "Height", 1080);
        bool Fullscreen = ConfigSystem::Get().GetBool("Window", "Fullscreen", false);
        bool VSync = ConfigSystem::Get().GetBool("Window", "VSync", true);
        bool Resizable = ConfigSystem::Get().GetBool("Window", "Resizable", true);
        bool Visible = ConfigSystem::Get().GetBool("Window", "Visible", true);
        RHI::API api = static_cast<RHI::API>(ConfigSystem::Get().GetUInt("Window", "Backend", static_cast<uint32_t>(BackendType::Vulkan)));
        WindowProps props(Title, api, Width, Height, Fullscreen, VSync, Resizable, Visible);

        uint32_t NumWorkers = ConfigSystem::Get().GetUInt("Workers", "Number", 0);
        if (NumWorkers > std::thread::hardware_concurrency()) // limit to avaible cores
        {
            NumWorkers = 0;
        }

        m_Window = std::make_unique<Zero::Window>(props);
        m_Window->SetEventCallback(BIND_EVENT_FN(Application::OnEvent));
        
        ShaderTarget compilerTarget = ShaderTarget::SPIRV;
        switch(api)
        {
            case RHI::API::Vulkan: compilerTarget = ShaderTarget::SPIRV; break;
            case RHI::API::OpenGL: compilerTarget = ShaderTarget::GLSL; break;
            case RHI::API::D3D12: compilerTarget = ShaderTarget::DXIL; break;
            case RHI::API::Metal: compilerTarget = ShaderTarget::MSL; break;
            default:
                ZERO_CORE_ASSERT(false, "Invalid backend type.");
                break;
        }
        ShaderCompiler::Get().Init(compilerTarget);

        Renderer::Get().Init(api, m_Window.get());
        SceneManager::Get().Init();
        BlockingThreadPool::Get().Init(NumWorkers);
        FileWatcher::Get().Init();

        auto imGuiLayer = std::make_unique<ImGuiLayer>();
        m_ImGuiLayer = imGuiLayer.get();
        PushOverlay(std::move(imGuiLayer));
    }

    Application::~Application()
    {
        FileWatcher::Get().Shutdown();
        m_LayerStack.reset(); // Destroy layers first to make sure no user code is generating work 
        Renderer::Get().Shutdown();
        SceneManager::Get().Shutdown();
        BlockingThreadPool::Get().Shutdown();
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
        ZERO_CORE_TRACE("Application started.");
        ZERO_PROFILE_THREAD("Main Thread");

        Time::Init();

        // Main loop
        uint64_t frameIndex = 0;
        while (m_Running)
        {
            Time::Update();
            m_Window->PollEvents(); // Poll OS events
            Input::UpdateInput();
            FileWatcher::Get().Poll();

            if (m_Minimized)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                continue;
            }

            float dt = Time::GetDeltaTime();

            // Game logic
            for (auto& layer : *m_LayerStack)
            {
                layer->OnUpdate(dt);
            }

            m_ImGuiLayer->Begin();

            for (auto& layer : *m_LayerStack)
            {
                layer->OnImGuiRender();
            }

            SceneManager::Get().FlushCommands();

            // Build frame
            Renderer::Get().RequestFrame(); // must add a timeout

            FrameData* frame = SceneManager::Get().BuildFrame(frameIndex, dt);

            m_ImGuiLayer->End(frame);

            Renderer::Get().SubmitFrame(frame);
            frameIndex++;
            ZERO_PROFILE_FRAME();
        }
    }

    void Application::OnEvent(Event& e)
    {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<WindowCloseEvent>(BIND_EVENT_FN(Application::OnWindowClose));
        dispatcher.Dispatch<RenderSurfaceResize>(BIND_EVENT_FN(Application::OnRenderSurfaceResize));

        ZERO_CORE_ASSERT(m_LayerStack, "LayerStack is null!");

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
        if (e.GetWidth() == 0 || e.GetHeight() == 0)
        {
            m_Minimized = true;
            return false;
        }

        m_Minimized = false;
        Renderer::Get().OnRenderSurfaceResize();
        return true; // the layers don't need to see this event
    }

}
