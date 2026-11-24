#pragma once
#include <cstdint>
#include <Engine/Export.h>
#include <Engine/Ecs/Scheduler.h>

namespace Zero
{
    class Application;
    class EcsRegistry;
    class PhysicsWorld;
    struct EcsCommandQueue;
    struct SystemDescriptor;
    template<typename T> struct EcsIntegration;

    class PhysicsSyncSystem
    {
    public:
        ZERO_API explicit PhysicsSyncSystem(PhysicsWorld& world) noexcept;

        // Drains RigidBodyComponent tombstones and destroys corresponding Jolt bodies.
        // Safe to run in PrePhysics / RenderSync so 0-substep frames never leak bodies.
        ZERO_API void TeardownTombstones(EcsRegistry& reg);

        // Full FixedStep pass:
        //   1. Tombstone teardown (drain RigidBodyComponent::Removed())
        //   2. Snapshot TransformComponent -> PreviousTransformComponent
        //   3. Gameplay push for existing bodies (modifiedEpoch > lastSynced && source != Physics)
        //   4. Jolt body creation for RigidBodyComponent with InvalidBodyID
        //   5. PhysicsWorld::Step(fixedDt)
        //   6. Active dynamic body write-back tagged WriteSource::Physics
        ZERO_API void Execute(EcsRegistry& reg,
                              EcsCommandQueue& queue,
                              float dt,
                              uint64_t epoch);

        [[nodiscard]] ZERO_API SystemDescriptor Descriptor();
        [[nodiscard]] ZERO_API SystemDescriptor TeardownDescriptor(SchedulePhase phase = SchedulePhase::PrePhysics);

        [[nodiscard]] uint64_t LastSyncedEpoch() const noexcept { return m_lastSyncedEpoch; }

    private:
        PhysicsWorld& m_world;
        uint64_t      m_lastSyncedEpoch = 0;
    };

    template<>
    struct EcsIntegration<PhysicsSyncSystem>
    {
        template<typename... Args>
        static void PushComponent(EcsRegistry&, Args&&...) noexcept {}

        ZERO_API static void PushSystem(Application& app, PhysicsWorld& world);
    };
}
