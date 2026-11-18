#pragma once
#include "Core.h"
#include <memory>
#include <deque>
#include <functional>
#include <Engine/Thread/Mutex.h>

namespace Zero
{
    class Event;
    class LayerStack;
    class Layer;
    class Window;
    class WindowCloseEvent;
    class RenderSurfaceResize;
    class ImGuiLayer;
    class EcsRegistry;
    struct SystemDescriptor;
    struct EcsState;

    template<typename T> struct EcsIntegration; // defined by the ECS module

    class ZERO_API Application
    {
    public:
        Application();
        virtual ~Application();

        void PushLayer(std::unique_ptr<Layer> layer);
        void PushOverlay(std::unique_ptr<Layer> overlay);

        // ECS extension points — thread-safe.
        //
        // PushComponent<T>() / PushSystem<T>() do NOT touch the ECS directly.
        // They enqueue a type-erased command that the main game thread
        // executes at the start of the next frame, so registration from any
        // thread (or before Run()) is safe.
        //
        // The actual work lives in EcsIntegration<T>, defined by the ECS
        // module — the engine core never sees component/system types.

        // Registers component type T with the ECS (creates its pool).
        template<typename T, typename... Args>
        void PushComponent(Args&&... args)
        {
            EnqueueEcsCommand([this, ...a = std::forward<Args>(args)](EcsRegistry& reg) mutable {
                EcsIntegration<T>::PushComponent(reg, std::move(a)...);
            });
        }

        // Constructs system T(args...) and registers it with the ECS scheduler.
        template<typename T, typename... Args>
        void PushSystem(Args&&... args)
        {
            EnqueueEcsCommand([this, ...a = std::forward<Args>(args)](EcsRegistry&) mutable {
                EcsIntegration<T>::PushSystem(*this, std::move(a)...);
            });
        }

        // ECS module API — stores a system descriptor plus the instance that
        // backs its execute() lambda. Game thread only.
        void RegisterEcsSystem(SystemDescriptor desc, std::shared_ptr<void> holder);

        static Application& Get();
        ZERO_FORCE_INLINE Window& GetWindow() { return *m_Window; }

        void Run();

    private:
        // Event handling
        void OnEvent(Event& e);
        bool OnWindowClose(WindowCloseEvent& e);
        bool OnRenderSurfaceResize(RenderSurfaceResize& e);

        // ECS command queue
        void EnqueueEcsCommand(std::function<void(EcsRegistry&)> cmd);
        void FlushEcsCommands(EcsRegistry& reg); // game thread, once per frame

    private:
        std::unique_ptr<class Window> m_Window;
        std::unique_ptr<LayerStack> m_LayerStack;
        ImGuiLayer* m_ImGuiLayer = nullptr;
        static Application* s_Instance;
        bool m_Running = true;
        bool m_Minimized = false;

        // pimpl for ecs
        std::unique_ptr<EcsState> m_ecsState;
        std::deque<std::function<void(EcsRegistry&)>> m_ecsCommands;
        Zero::Mutex m_ecsCommandMutex;
    };

    // To be defined in client
    Application* CreateApplication();
}
