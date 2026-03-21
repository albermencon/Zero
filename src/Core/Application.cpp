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
//temp
#include "ImGui/Backends/Vulkan/ImGuiLayerVulkan.h"
#include "Graphics/backend/Vulkan/VulkanContext.h"
#include <imgui.h>

#include "Graphics/Renderer/Renderer.h"

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

        if (false && props.backend != BackendType::Vulkan)
        {
            auto imguiLayer = std::make_unique<ImGuiLayerVulkan>(
                static_cast<VulkanDevice*>(m_backend.get())
            );
            //PushOverlay(std::move(imguiLayer));
        }

        class ImGuiTestLayer : public Layer
        {
        public:
            ImGuiTestLayer() : Layer("ImGuiTest") {}
            void OnRender() override
            {
                float dt = Time::GetDeltaTime();
                float fps = dt > 0.0f ? 1.0f / dt : 0.0f;

                static bool toggled = false;

                ImGui::Begin("Performance");
                ImGui::SetWindowFontScale(1.5f); // scale factor (e.g. 1.5x)
                ImGui::Text("Delta time: %.3f ms", dt * 1000.0f);
                ImGui::Text("FPS: %.1f", fps);
                ImGui::End();

                //ImGui::ShowDemoWindow();
            }
        };

        PushLayer(std::make_unique<ImGuiTestLayer>());
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

            // Build frame
            Renderer::Get().RequestFrame();

            //ImGui::NewFrame();
            for (auto& layer : *m_LayerStack)
            {
                //layer->OnRender();
            }
            //ImGui::EndFrame();

            auto frame = std::make_unique<FrameData>();

            for (auto& layer : *m_LayerStack)
            {
                layer->OnBuildFrame(*frame);
            }

            Renderer::Get().SubmitFrame(std::move(frame));
        }

        Renderer::Get().Shutdown();
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
