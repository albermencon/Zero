#include "pch.h"
#include <algorithm>
#include <cmath>
#include <memory>

#include <Engine/Application.h>
#include <Engine/Ecs/Systems/PhysicsSyncSystem.h>
#include <Engine/Ecs/EcsRegistry.h>
#include <Engine/Ecs/EcsCommandQueue.h>
#include <Engine/Ecs/Scheduler.h>
#include <Engine/Ecs/Components/TransformComponent.h>
#include <Engine/Ecs/Components/PreviousTransformComponent.h>
#include <Engine/Ecs/Components/RigidBodyComponent.h>
#include <Engine/Ecs/Components/ColliderComponent.h>
#include <Engine/Ecs/Components/BoundsComponent.h>
#include <Engine/Physics/PhysicsSystemAPI.h>

namespace Zero
{
    PhysicsSyncSystem::PhysicsSyncSystem(PhysicsWorld& world) noexcept
        : m_world(world)
    {
    }

    void PhysicsSyncSystem::TeardownTombstones(EcsRegistry& reg)
    {
        if (!m_world.IsInitialized())
            return;

        const auto* rbPool = reg.TryPool<RigidBodyComponent>();
        if (!rbPool)
            return;

        const auto removed = rbPool->Removed();
        for (const auto& item : removed)
        {
            if (item.value.bodyId.IsValid())
            {
                m_world.DestroyBody(item.value.bodyId);
            }
        }
    }

    void PhysicsSyncSystem::Execute(EcsRegistry& reg,
                                    EcsCommandQueue& /*queue*/,
                                    float dt,
                                    uint64_t epoch)
    {
        if (!m_world.IsInitialized())
            return;

        // 1. Tombstone Teardown — drain RigidBodyComponent::Removed() before anything else
        TeardownTombstones(reg);

        auto* rbPool               = reg.TryPool<RigidBodyComponent>();
        auto* tfPool               = reg.TryPool<TransformComponent>();
        auto* prevPool             = reg.TryPool<PreviousTransformComponent>();
        const auto* colliderPool   = reg.TryPool<ColliderComponent>();
        const auto* boundsPool     = reg.TryPool<BoundsComponent>();

        // 2a. Snapshot TransformComponent -> PreviousTransformComponent for render interpolation
        if (prevPool && tfPool)
        {
            auto prevWrite = prevPool->WriteView(epoch, WriteSource::Physics);
            const uint32_t prevCount = prevWrite.Count();
            for (uint32_t i = 0; i < prevCount; ++i)
            {
                const uint32_t entityIdx = prevWrite.entities[i];
                if (const TransformComponent* tf = tfPool->Get(entityIdx))
                {
                    prevWrite.Write(i, PreviousTransformComponent::FromTransform(*tf));
                }
            }
        }

        // 2b/2c. Body creation & gameplay transform/velocity push
        if (rbPool && tfPool)
        {
            auto rbWrite       = rbPool->WriteView(epoch, WriteSource::Physics);
            const auto tfRead  = tfPool->ReadView();
            const uint32_t rbCount = rbWrite.Count();

            for (uint32_t i = 0; i < rbCount; ++i)
            {
                const uint32_t entityIdx = rbWrite.entities[i];
                const TransformComponent* tf = tfPool->Get(entityIdx);
                if (!tf)
                    continue;

                RigidBodyComponent& rb = rbWrite.data[i];

                if (rb.bodyId.IsInvalid())
                {
                    BodyDesc desc;
                    desc.position        = tf->position;
                    desc.rotation        = tf->rotation.Normalized();
                    desc.scale           = tf->scale;
                    desc.linearVelocity  = rb.linearVelocity;
                    desc.angularVelocity = rb.angularVelocity;
                    desc.userData        = reg.CurrentGeneration(entityIdx);

                    ShapeID tempShape = InvalidShapeID;
                    if (const ColliderComponent* col = colliderPool ? colliderPool->Get(entityIdx) : nullptr)
                    {
                        desc.motion         = col->motion;
                        desc.layer          = col->layer;
                        desc.friction       = col->friction;
                        desc.restitution    = col->restitution;
                        desc.linearDamping  = col->linearDamping;
                        desc.angularDamping = col->angularDamping;
                        desc.gravityFactor  = col->gravityFactor;
                        desc.isSensor       = col->isSensor;
                        desc.startActivated = col->startActivated;
                        desc.allowSleeping  = col->allowSleeping;

                        if (desc.motion == MotionType::Dynamic)
                        {
                            desc.mass = (rb.invMass > 0.0f)
                                ? (1.0f / rb.invMass)
                                : (col->mass > 0.0f ? col->mass : 1.0f);
                            rb.invMass = 1.0f / desc.mass;
                        }
                        else
                        {
                            desc.mass  = 0.0f;
                            rb.invMass = 0.0f;
                        }

                        if (col->shape.IsValid())
                        {
                            desc.shape = col->shape;
                        }
                        else if (col->shapeType == ColliderShapeType::Box)
                        {
                            desc.fallbackHalfExtents = col->halfExtents;
                        }
                        else if (col->shapeType == ColliderShapeType::Sphere)
                        {
                            const float maxScale = std::max({
                                std::fabs(tf->scale.x),
                                std::fabs(tf->scale.y),
                                std::fabs(tf->scale.z)
                            });
                            tempShape  = m_world.CreateSphereShape(col->radius * std::max(maxScale, 1e-3f));
                            desc.shape = tempShape;
                        }
                        else if (col->shapeType == ColliderShapeType::Capsule)
                        {
                            tempShape  = m_world.CreateCapsuleShape(col->halfHeight, col->radius);
                            desc.shape = tempShape;
                        }
                    }
                    else
                    {
                        if (rb.invMass > 0.0f)
                        {
                            desc.motion = MotionType::Dynamic;
                            desc.mass   = 1.0f / rb.invMass;
                            desc.layer  = PhysicsLayer::Dynamic;
                        }
                        else
                        {
                            desc.motion = MotionType::Static;
                            desc.mass   = 0.0f;
                            desc.layer  = PhysicsLayer::Static;
                        }

                        if (const BoundsComponent* bounds = boundsPool ? boundsPool->Get(entityIdx) : nullptr)
                        {
                            const Vec3 ext = (bounds->max - bounds->min) * 0.5f;
                            if (ext.x > 1e-4f && ext.y > 1e-4f && ext.z > 1e-4f)
                            {
                                desc.fallbackHalfExtents = ext;
                                desc.scale = Vec3{ 1.0f, 1.0f, 1.0f };
                            }
                        }
                    }

                    rb.bodyId = m_world.CreateBody(desc, true);
                    if (tempShape.IsValid())
                        m_world.DestroyShape(tempShape);

                    rbWrite.MarkDirty(i);
                    continue;
                }

                // Gameplay Transform push (never push WriteSource::Physics)
                const uint32_t tfDense = tfPool->sparse[entityIdx];
                if (tfDense < tfRead.Count())
                {
                    const bool tfDirty = tfRead.epochs[tfDense] > m_lastSyncedEpoch &&
                                         tfRead.sources[tfDense] != WriteSource::Physics;
                    if (tfDirty)
                    {
                        const MotionType motion = m_world.GetMotionType(rb.bodyId);
                        if (motion == MotionType::Kinematic)
                        {
                            m_world.MoveKinematic(rb.bodyId, tf->position, tf->rotation, dt);
                        }
                        else if (motion == MotionType::Dynamic)
                        {
                            m_world.SetPositionAndRotation(rb.bodyId, tf->position, tf->rotation, true);
                        }
                        else
                        {
                            m_world.SetPositionAndRotation(rb.bodyId, tf->position, tf->rotation, false);
                        }
                    }
                }

                // Gameplay velocity push (if modified externally)
                const bool rbDirty = rbWrite.epochs[i] > m_lastSyncedEpoch &&
                                     rbWrite.sources[i] != WriteSource::Physics;
                if (rbDirty)
                {
                    const MotionType motion = m_world.GetMotionType(rb.bodyId);
                    if (motion != MotionType::Static)
                    {
                        m_world.SetLinearVelocity(rb.bodyId, rb.linearVelocity);
                        m_world.SetAngularVelocity(rb.bodyId, rb.angularVelocity);
                    }
                }
            }
        }

        // 3. Simulation step
        m_world.Step(dt, 1);

        // 4. Active dynamic body write-back (zero heap allocations, WriteSource::Physics)
        if (rbPool && tfPool)
        {
            constexpr uint32_t kBatchSize = 256;
            ActiveBodyState batch[kBatchSize];

            auto tfWrite = tfPool->WriteView(epoch, WriteSource::Physics);
            auto rbWrite = rbPool->WriteView(epoch, WriteSource::Physics);

            uint32_t cursor = 0;
            const uint32_t totalActive = m_world.GetActiveBodyCount();
            while (cursor < totalActive)
            {
                const uint32_t written = m_world.ReadActiveDynamicBodies(cursor, batch);
                for (uint32_t j = 0; j < written; ++j)
                {
                    const ActiveBodyState& state = batch[j];
                    if (state.entity == Invalid || !reg.IsAlive(state.entity))
                        continue;

                    const uint32_t entityIdx = Index(state.entity);

                    if (tfPool->Has(entityIdx))
                    {
                        const uint32_t tfDense = tfPool->sparse[entityIdx];
                        tfWrite.data[tfDense].position = state.position;
                        tfWrite.data[tfDense].rotation = state.rotation;
                        tfWrite.MarkDirty(tfDense);
                    }

                    if (rbPool->Has(entityIdx))
                    {
                        const uint32_t rbDense = rbPool->sparse[entityIdx];
                        rbWrite.data[rbDense].linearVelocity  = state.linearVelocity;
                        rbWrite.data[rbDense].angularVelocity = state.angularVelocity;
                        rbWrite.MarkDirty(rbDense);
                    }
                }
            }
        }

        m_lastSyncedEpoch = std::max(m_lastSyncedEpoch, reg.CurrentEpoch());
    }

    SystemDescriptor PhysicsSyncSystem::Descriptor()
    {
        SystemDescriptor desc;
        desc.name     = "PhysicsSyncSystem";
        desc.phase    = SchedulePhase::FixedStep;
        desc.affinity = ThreadAffinity::MainThread;
        desc.access.reads = MaskOf<
            TransformComponent,
            PreviousTransformComponent,
            RigidBodyComponent,
            ColliderComponent,
            BoundsComponent>();
        desc.access.writes = MaskOf<
            TransformComponent,
            PreviousTransformComponent,
            RigidBodyComponent>();
        desc.access.resourceReads  = ResourceBit::PhysicsWorld;
        desc.access.resourceWrites = ResourceBit::PhysicsWorld;
        desc.execute = [this](EcsRegistry& reg, EcsCommandQueue& queue, float dt, uint64_t epoch) {
            Execute(reg, queue, dt, epoch);
        };
        return desc;
    }

    SystemDescriptor PhysicsSyncSystem::TeardownDescriptor(SchedulePhase phase)
    {
        SystemDescriptor desc;
        desc.name                  = "PhysicsTombstoneTeardown";
        desc.phase                 = phase;
        desc.affinity              = ThreadAffinity::MainThread;
        desc.access.reads          = MaskOf<RigidBodyComponent>();
        desc.access.resourceWrites = ResourceBit::PhysicsWorld;
        desc.execute = [this](EcsRegistry& reg, EcsCommandQueue&, float, uint64_t) {
            TeardownTombstones(reg);
        };
        return desc;
    }

    void EcsIntegration<PhysicsSyncSystem>::PushSystem(Application& app, PhysicsWorld& world)
    {
        auto holder = std::make_shared<PhysicsSyncSystem>(world);
        app.RegisterEcsSystem(holder->TeardownDescriptor(SchedulePhase::PrePhysics), holder);
        app.RegisterEcsSystem(holder->Descriptor(), holder);
        app.RegisterEcsSystem(holder->TeardownDescriptor(SchedulePhase::RenderSync), std::move(holder));
    }
}
