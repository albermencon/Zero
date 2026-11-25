#include <benchmark/benchmark.h>

#include <Engine/Physics/PhysicsSystemAPI.h>
#include <Engine/Ecs/EcsRegistry.h>
#include <Engine/Ecs/EcsCommandQueue.h>
#include <Engine/Ecs/Systems/PhysicsSyncSystem.h>
#include <Engine/Ecs/Components/TransformComponent.h>
#include <Engine/Ecs/Components/PreviousTransformComponent.h>
#include <Engine/Ecs/Components/RigidBodyComponent.h>
#include <Engine/Ecs/Components/ColliderComponent.h>

#include "JobSystem/BlockingThreadPool.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace
{
    using namespace Zero;

    struct BenchPoolGuard
    {
        explicit BenchPoolGuard(uint32_t threads = 0)
        {
            BlockingThreadPool::Get().Init(threads);
        }
        ~BenchPoolGuard()
        {
            BlockingThreadPool::Get().Shutdown();
        }
    };

    // -------------------------------------------------------------------------
    // 1. PhysicsWorld::Step throughput with N active dynamic bodies
    // -------------------------------------------------------------------------
    void BM_Physics_Step_DynamicBodies(benchmark::State& state)
    {
        BenchPoolGuard poolGuard(0);
        const uint32_t count = static_cast<uint32_t>(state.range(0));

        PhysicsWorld world;
        auto cfg = PhysicsWorldConfig::Default();
        cfg.maxBodies        = std::max(cfg.maxBodies, count + 64);
        cfg.maxContactEvents = 16384;
        cfg.assertOnContactOverflow = false;
        world.Init(cfg);

        const ShapeID floorShape = world.CreateBoxShape(Vec3{ 200.0f, 1.0f, 200.0f });
        const ShapeID boxShape   = world.CreateBoxShape(Vec3{ 0.4f, 0.4f, 0.4f });

        BodyDesc floorDesc;
        floorDesc.motion   = MotionType::Static;
        floorDesc.shape    = floorShape;
        floorDesc.position = RVec3{ 0.0f, -1.0f, 0.0f };
        floorDesc.layer    = PhysicsLayer::Static;
        (void)world.CreateBody(floorDesc);

        std::vector<BodyDesc> descs(count);
        std::vector<BodyID>   ids(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t gx = i % 64;
            const uint32_t gz = i / 64;
            descs[i].motion        = MotionType::Dynamic;
            descs[i].shape         = boxShape;
            descs[i].position      = RVec3{
                (static_cast<Real>(gx) - Real(32.0)) * Real(1.5),
                Real(5.0) + static_cast<Real>(i % 8) * Real(1.1),
                (static_cast<Real>(gz) - Real(32.0)) * Real(1.5)
            };
            descs[i].layer         = PhysicsLayer::Dynamic;
            descs[i].allowSleeping = false;
        }
        world.CreateBodies(descs, ids);
        world.OptimizeBroadPhase();

        constexpr float kDt = 1.0f / 60.0f;
        for (auto _ : state)
        {
            world.ClearContactEvents();
            world.Step(kDt, 1);
            // Keep bodies gently alive/falling within arena
            benchmark::DoNotOptimize(world.GetActiveBodyCount());
        }

        state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * count);
        world.DestroyShape(boxShape);
        world.DestroyShape(floorShape);
    }
    BENCHMARK(BM_Physics_Step_DynamicBodies)
        ->Arg(256)
        ->Arg(1024)
        ->Arg(4096)
        ->Unit(benchmark::kMicrosecond);

    // -------------------------------------------------------------------------
    // 2. Full PhysicsSyncSystem::Execute() FixedStep (prev snapshot + step + write-back)
    // -------------------------------------------------------------------------
    void BM_Physics_SyncSystem_FixedStep(benchmark::State& state)
    {
        BenchPoolGuard poolGuard(0);
        const uint32_t count = static_cast<uint32_t>(state.range(0));

        PhysicsWorld world;
        auto cfg = PhysicsWorldConfig::Default();
        cfg.maxBodies        = std::max(cfg.maxBodies, count + 64);
        cfg.maxContactEvents = 16384;
        cfg.assertOnContactOverflow = false;
        world.Init(cfg);

        EcsRegistry reg;
        EcsCommandQueue q;
        PhysicsSyncSystem sync(world);

        const ShapeID floorShape = world.CreateBoxShape(Vec3{ 200.0f, 1.0f, 200.0f });
        const ShapeID sphereShape = world.CreateSphereShape(0.4f);

        Entity floorEnt = reg.CreateEntity();
        reg.AddComponent(floorEnt, TransformComponent{
            RVec3{ 0.0f, -1.0f, 0.0f }, Quat::Identity(), Vec3{ 1.0f, 1.0f, 1.0f }
        }, q);
        reg.AddComponent(floorEnt, RigidBodyComponent{ Vec3{}, 0.0f, Vec3{}, InvalidBodyID }, q);
        reg.AddComponent(floorEnt, ColliderComponent::FromShape(
            floorShape, MotionType::Static, PhysicsLayer::Static), q);

        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t gx = i % 64;
            const uint32_t gz = i / 64;
            Entity e = reg.CreateEntity();
            reg.AddComponent(e, TransformComponent{
                RVec3{
                    (static_cast<Real>(gx) - Real(32.0)) * Real(1.5),
                    Real(2.0) + static_cast<Real>(i % 4) * Real(1.2),
                    (static_cast<Real>(gz) - Real(32.0)) * Real(1.5)
                },
                Quat::Identity(),
                Vec3{ 1.0f, 1.0f, 1.0f }
            }, q);
            reg.AddComponent(e, PreviousTransformComponent::FromTransform(TransformComponent::Identity()), q);
            reg.AddComponent(e, RigidBodyComponent{ Vec3{ 0.1f, 0.0f, 0.0f }, 1.0f, Vec3{}, InvalidBodyID }, q);
            auto col = ColliderComponent::FromShape(sphereShape, MotionType::Dynamic, PhysicsLayer::Dynamic);
            col.allowSleeping = false;
            reg.AddComponent(e, col, q);
        }
        reg.FlushCommands(q);

        constexpr float kDt = 1.0f / 60.0f;
        // Warm-up step creates all Jolt bodies
        world.ClearContactEvents();
        sync.Execute(reg, q, kDt, reg.AdvanceEpoch());
        world.OptimizeBroadPhase();

        for (auto _ : state)
        {
            world.ClearContactEvents();
            sync.Execute(reg, q, kDt, reg.AdvanceEpoch());
            benchmark::DoNotOptimize(world.GetActiveBodyCount());
        }

        state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * count);
        world.Shutdown();
    }
    BENCHMARK(BM_Physics_SyncSystem_FixedStep)
        ->Arg(256)
        ->Arg(1024)
        ->Arg(4096)
        ->Unit(benchmark::kMicrosecond);

    // -------------------------------------------------------------------------
    // 3. Active-only write-back scaling: 4096 sleeping/static bodies vs 32 active
    // -------------------------------------------------------------------------
    void BM_Physics_SyncSystem_4096Sleeping_32Active(benchmark::State& state)
    {
        BenchPoolGuard poolGuard(0);

        PhysicsWorld world;
        auto cfg = PhysicsWorldConfig::Default();
        cfg.gravity = Vec3{ 0.0f, 0.0f, 0.0f };
        world.Init(cfg);

        EcsRegistry reg;
        EcsCommandQueue q;
        PhysicsSyncSystem sync(world);

        const ShapeID boxShape = world.CreateBoxShape(Vec3{ 0.4f, 0.4f, 0.4f });

        // 4096 sleeping dynamic bodies
        for (uint32_t i = 0; i < 4096; ++i)
        {
            Entity e = reg.CreateEntity();
            reg.AddComponent(e, TransformComponent{
                RVec3{ static_cast<Real>(i % 64) * Real(3.0), Real(0.0), static_cast<Real>(i / 64) * Real(3.0) },
                Quat::Identity(),
                Vec3{ 1.0f, 1.0f, 1.0f }
            }, q);
            reg.AddComponent(e, RigidBodyComponent{ Vec3{}, 1.0f, Vec3{}, InvalidBodyID }, q);
            auto col = ColliderComponent::FromShape(boxShape, MotionType::Dynamic, PhysicsLayer::Dynamic);
            col.startActivated = false;
            reg.AddComponent(e, col, q);
        }

        // 32 active dynamic bodies with velocity
        for (uint32_t i = 0; i < 32; ++i)
        {
            Entity e = reg.CreateEntity();
            reg.AddComponent(e, TransformComponent{
                RVec3{ static_cast<Real>(i) * Real(4.0), Real(20.0), Real(0.0) },
                Quat::Identity(),
                Vec3{ 1.0f, 1.0f, 1.0f }
            }, q);
            reg.AddComponent(e, PreviousTransformComponent::FromTransform(TransformComponent::Identity()), q);
            reg.AddComponent(e, RigidBodyComponent{ Vec3{ 0.0f, 0.0f, 1.0f }, 1.0f, Vec3{}, InvalidBodyID }, q);
            auto col = ColliderComponent::FromShape(boxShape, MotionType::Dynamic, PhysicsLayer::Dynamic);
            col.allowSleeping = false;
            reg.AddComponent(e, col, q);
        }
        reg.FlushCommands(q);

        constexpr float kDt = 1.0f / 60.0f;
        world.ClearContactEvents();
        sync.Execute(reg, q, kDt, reg.AdvanceEpoch());
        world.OptimizeBroadPhase();

        for (auto _ : state)
        {
            world.ClearContactEvents();
            sync.Execute(reg, q, kDt, reg.AdvanceEpoch());
            benchmark::DoNotOptimize(world.GetActiveBodyCount());
        }

        state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * 32);
        world.Shutdown();
    }
    BENCHMARK(BM_Physics_SyncSystem_4096Sleeping_32Active)->Unit(benchmark::kMicrosecond);

    // -------------------------------------------------------------------------
    // 4. Zero-allocation Spatial Queries across 1,000 bodies
    // -------------------------------------------------------------------------
    void BM_Physics_Raycast_Single(benchmark::State& state)
    {
        PhysicsWorld world;
        world.Init(PhysicsWorldConfig::Default());

        const ShapeID boxShape = world.CreateBoxShape(Vec3{ 0.5f, 0.5f, 0.5f });
        std::vector<BodyDesc> descs(1000);
        std::vector<BodyID>   ids(1000);
        for (uint32_t i = 0; i < 1000; ++i)
        {
            descs[i].motion   = MotionType::Static;
            descs[i].shape    = boxShape;
            descs[i].layer    = PhysicsLayer::Static;
            descs[i].position = RVec3{
                static_cast<Real>(i % 10) * Real(3.0),
                static_cast<Real>((i / 10) % 10) * Real(3.0),
                static_cast<Real>(i / 100) * Real(3.0) + Real(5.0)
            };
        }
        world.CreateBodies(descs, ids);
        world.OptimizeBroadPhase();

        HitResult hit{};
        uint32_t qIdx = 0;
        for (auto _ : state)
        {
            const Real x = static_cast<Real>(qIdx % 10) * Real(3.0);
            const Real y = static_cast<Real>((qIdx / 10) % 10) * Real(3.0);
            bool ok = world.Raycast(RVec3{ x, y, Real(0.0) }, Vec3{ 0.0f, 0.0f, 1.0f }, 100.0f, kAllLayersMask, hit);
            benchmark::DoNotOptimize(ok);
            benchmark::DoNotOptimize(hit);
            ++qIdx;
        }

        state.SetItemsProcessed(state.iterations());
        world.DestroyShape(boxShape);
    }
    BENCHMARK(BM_Physics_Raycast_Single)->Unit(benchmark::kNanosecond);

    void BM_Physics_RaycastAll_Span(benchmark::State& state)
    {
        PhysicsWorld world;
        world.Init(PhysicsWorldConfig::Default());

        const ShapeID boxShape = world.CreateBoxShape(Vec3{ 0.5f, 0.5f, 0.5f });
        std::vector<BodyDesc> descs(1000);
        std::vector<BodyID>   ids(1000);
        for (uint32_t i = 0; i < 1000; ++i)
        {
            descs[i].motion   = MotionType::Static;
            descs[i].shape    = boxShape;
            descs[i].layer    = PhysicsLayer::Static;
            descs[i].position = RVec3{
                static_cast<Real>(i % 10) * Real(3.0),
                static_cast<Real>((i / 10) % 10) * Real(3.0),
                static_cast<Real>(i / 100) * Real(3.0) + Real(5.0)
            };
        }
        world.CreateBodies(descs, ids);
        world.OptimizeBroadPhase();

        std::array<HitResult, 16> hits{};
        uint32_t qIdx = 0;
        for (auto _ : state)
        {
            const Real x = static_cast<Real>(qIdx % 10) * Real(3.0);
            const Real y = static_cast<Real>((qIdx / 10) % 10) * Real(3.0);
            uint32_t n = world.RaycastAll(RVec3{ x, y, Real(0.0) }, Vec3{ 0.0f, 0.0f, 1.0f }, 100.0f, kAllLayersMask, hits);
            benchmark::DoNotOptimize(n);
            benchmark::DoNotOptimize(hits.data());
            ++qIdx;
        }

        state.SetItemsProcessed(state.iterations());
        world.DestroyShape(boxShape);
    }
    BENCHMARK(BM_Physics_RaycastAll_Span)->Unit(benchmark::kNanosecond);

    void BM_Physics_OverlapSphere_Span(benchmark::State& state)
    {
        PhysicsWorld world;
        world.Init(PhysicsWorldConfig::Default());

        const ShapeID boxShape = world.CreateBoxShape(Vec3{ 0.5f, 0.5f, 0.5f });
        std::vector<BodyDesc> descs(1000);
        std::vector<BodyID>   ids(1000);
        for (uint32_t i = 0; i < 1000; ++i)
        {
            descs[i].motion   = MotionType::Static;
            descs[i].shape    = boxShape;
            descs[i].layer    = PhysicsLayer::Static;
            descs[i].position = RVec3{
                static_cast<Real>(i % 10) * Real(3.0),
                static_cast<Real>((i / 10) % 10) * Real(3.0),
                static_cast<Real>(i / 100) * Real(3.0) + Real(5.0)
            };
        }
        world.CreateBodies(descs, ids);
        world.OptimizeBroadPhase();

        std::array<BodyID, 64> outBodies{};
        for (auto _ : state)
        {
            uint32_t n = world.OverlapSphere(RVec3{ Real(13.5), Real(13.5), Real(18.5) }, 5.0f, kAllLayersMask, outBodies);
            benchmark::DoNotOptimize(n);
            benchmark::DoNotOptimize(outBodies.data());
        }

        state.SetItemsProcessed(state.iterations());
        world.DestroyShape(boxShape);
    }
    BENCHMARK(BM_Physics_OverlapSphere_Span)->Unit(benchmark::kNanosecond);

    // -------------------------------------------------------------------------
    // 5. Batch CreateBodies + DestroyBodies throughput (streaming world chunks)
    // -------------------------------------------------------------------------
    void BM_Physics_BatchStreamCreateDestroy(benchmark::State& state)
    {
        PhysicsWorld world;
        world.Init(PhysicsWorldConfig::Default());

        const ShapeID boxShape = world.CreateBoxShape(Vec3{ 0.5f, 0.5f, 0.5f });
        constexpr uint32_t kBatch = 128;
        std::array<BodyDesc, kBatch> descs{};
        std::array<BodyID, kBatch>   ids{};

        for (uint32_t i = 0; i < kBatch; ++i)
        {
            descs[i].motion   = MotionType::Static;
            descs[i].shape    = boxShape;
            descs[i].layer    = PhysicsLayer::Static;
            descs[i].position = RVec3{ static_cast<Real>(i) * Real(2.0), Real(0.0), Real(0.0) };
        }

        uint32_t iter = 0;
        for (auto _ : state)
        {
            uint32_t n = world.CreateBodies(descs, ids, false);
            benchmark::DoNotOptimize(n);
            world.DestroyBodies(ids);

            // Jolt's lock-free BroadPhaseQuadTree defers node reclamation until
            // Update/OptimizeBroadPhase runs.
            if ((++iter & 7u) == 0u)
            {
                state.PauseTiming();
                world.OptimizeBroadPhase();
                state.ResumeTiming();
            }
        }

        state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * kBatch);
        world.DestroyShape(boxShape);
    }
    BENCHMARK(BM_Physics_BatchStreamCreateDestroy)->Unit(benchmark::kMicrosecond);
}
