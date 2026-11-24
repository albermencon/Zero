#pragma once
#include <Engine/Application.h>
#include <Engine/Ecs/EcsRegistry.h>
#include <Engine/Ecs/Scheduler.h>
#include <memory>
#include <type_traits>

namespace Zero
{
    // EcsIntegration<T> — the ECS module's hook into Application.
    //
    // Application::PushComponent<T>() / PushSystem<T>() route here. This
    // template is only instantiated in the consumer's TU, so the engine core
    // header (Application.h) never has to include ECS headers.
    template<typename T>
    struct EcsIntegration
    {
        // Registers component type T with the registry by creating its pool.
        // Component *values* are still attached per-entity via
        // EcsRegistry::AddComponent — this only makes the type known to the
        // ECS. Constructor args are reserved for future registration options.
        template<typename... Args>
        static void PushComponent(EcsRegistry& reg, Args&&... /*args*/)
        {
            static_assert(std::is_trivially_copyable_v<T>,
                "ECS components must be trivially copyable");
            (void)reg.Pool<T>();
        }

        // Constructs the system instance T(args...) and registers its
        // SystemDescriptor. T must expose `SystemDescriptor Descriptor()`.
        // The instance is owned by the Application (the descriptor's
        // execute() captures the instance's this, so it must outlive the
        // descriptor).
        template<typename... Args>
        static void PushSystem(Application& app, Args&&... args)
        {
            auto holder = std::make_shared<T>(std::move(args)...);
            SystemDescriptor desc = holder->Descriptor();
            app.RegisterEcsSystem(std::move(desc), std::move(holder));
        }
    };
}