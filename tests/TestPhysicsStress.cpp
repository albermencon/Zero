#include <doctest.h>

#include <Engine/Physics/PhysicsSystemAPI.h>
#include <Engine/Ecs/EcsRegistry.h>
#include <Engine/Ecs/EcsCommandQueue.h>
#include <Engine/Ecs/Scheduler.h>
#include <Engine/Ecs/Systems/PhysicsSyncSystem.h>
#include <Engine/Ecs/Components/TransformComponent.h>
#include <Engine/Ecs/Components/PreviousTransformComponent.h>
#include <Engine/Ecs/Components/RigidBodyComponent.h>
#include <Engine/Ecs/Components/ColliderComponent.h>

#include "JobSystem/BlockingThreadPool.h"

#include <array>
#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

using namespace Zero;

namespace
{
    struct StressPoolGuard
    {
        explicit StressPoolGuard(uint32_t threads = 4)
        {
            BlockingThreadPool::Get().Init(threads);
        }
        ~StressPoolGuard()
        {
            BlockingThreadPool::Get().Shutdown();
        }
    };
}

// ---------------------------------------------------------------------------
// 1. Multi-substep ContactEvent preservation (Substep 1 events survive Substep 2)
// ---------------------------------------------------------------------------
TEST_CASE("Physics Stress: Multi-substep frame preserves Substep 1 ContactEvents for PostPhysics")
{
    StressPoolGuard poolGuard(2);

    PhysicsWorld world;
    auto cfg = PhysicsWorldConfig::Default();
    cfg.gravity = Vec3{ 0.0f, 0.0f, 0.0f };
    REQUIRE(world.Init(cfg));

    // Static sensor trigger at origin
    BodyDesc sensorDesc;
    sensorDesc.motion   = MotionType::Static;
    sensorDesc.position = Vec3{ 0.0f, 0.0f, 0.0f };
    sensorDesc.layer    = PhysicsLayer::Trigger;
    sensorDesc.isSensor = true;
    sensorDesc.userData = Entity{ 10 };
    const BodyID sensorId = world.CreateBody(sensorDesc);
    REQUIRE(sensorId.IsValid());

    // Fast dynamic sphere moving along +X so it enters on Substep 1 and exits by Substep 2
    const ShapeID sphereShape = world.CreateSphereShape(0.25f);
    BodyDesc moverDesc;
    moverDesc.motion         = MotionType::Dynamic;
    moverDesc.shape          = sphereShape;
    moverDesc.position       = Vec3{ -0.7f, 0.0f, 0.0f };
    moverDesc.linearVelocity = Vec3{ 60.0f, 0.0f, 0.0f };
    moverDesc.linearDamping  = 0.0f;
    moverDesc.layer          = PhysicsLayer::Dynamic;
    moverDesc.userData       = Entity{ 20 };
    const BodyID moverId = world.CreateBody(moverDesc);
    REQUIRE(moverId.IsValid());

    constexpr float kDt = 1.0f / 60.0f;

    // Frame 1 begins: clear once before the 2-substep loop (mirrors Application::Run)
    world.ClearContactEvents();

    // Substep 1: collision runs at x=-0.7 (overlaps [-0.5, 0.5]) -> TriggerEnter, then integrates to x=+0.3
    world.Step(kDt, 1);
    // Substep 2: collision runs at x=+0.3 (still overlapping) -> Persisted, then integrates to x=+1.3
    world.Step(kDt, 1);

    // PostPhysics inspects GetContactEvents() after BOTH substeps:
    // MUST still contain Substep 1's TriggerEnter (not wiped by Substep 2)!
    bool sawEnter     = false;
    bool sawPersisted = false;
    for (const auto& ev : world.GetContactEvents())
    {
        if (ev.type == ContactType::TriggerEnter) sawEnter     = true;
        if (ev.type == ContactType::Persisted)    sawPersisted = true;
    }
    CHECK(sawEnter);
    CHECK(sawPersisted);

    // Next frame clears at frame start, then Substep 3 detects separation at x=+1.3 -> TriggerExit
    world.ClearContactEvents();
    CHECK(world.GetContactEvents().empty());

    world.Step(kDt, 1);
    bool sawExit = false;
    for (const auto& ev : world.GetContactEvents())
    {
        if (ev.type == ContactType::TriggerExit) sawExit = true;
    }
    CHECK(sawExit);

    world.DestroyShape(sphereShape);
}

// ---------------------------------------------------------------------------
// 2. ContactEvent buffer overflow drops cleanly with counter (zero allocation)
// ---------------------------------------------------------------------------
TEST_CASE("Physics Stress: ContactEvent buffer overflow drops cleanly without allocation")
{
    PhysicsWorld world;
    auto cfg = PhysicsWorldConfig::Default();
    cfg.maxContactEvents        = 8;
    cfg.assertOnContactOverflow = false;
    REQUIRE(world.Init(cfg));

    const ShapeID floorShape = world.CreateBoxShape(Vec3{ 50.0f, 0.5f, 50.0f });
    const ShapeID boxShape   = world.CreateBoxShape(Vec3{ 0.4f, 0.4f, 0.4f });

    BodyDesc floorDesc;
    floorDesc.motion   = MotionType::Static;
    floorDesc.shape    = floorShape;
    floorDesc.position = Vec3{ 0.0f, -0.5f, 0.0f };
    floorDesc.layer    = PhysicsLayer::Static;
    (void)world.CreateBody(floorDesc);

    std::array<BodyDesc, 32> boxes{};
    std::array<BodyID, 32>   ids{};
    for (uint32_t i = 0; i < 32; ++i)
    {
        boxes[i].motion   = MotionType::Dynamic;
        boxes[i].shape    = boxShape;
        boxes[i].position = Vec3{ static_cast<float>(i) * 1.2f, 0.35f, 0.0f };
        boxes[i].layer    = PhysicsLayer::Dynamic;
        boxes[i].userData = Entity{ 100 + i };
    }
    REQUIRE(world.CreateBodies(boxes, ids) == 32);

    world.ClearContactEvents();
    world.Step(1.0f / 60.0f);

    CHECK(world.GetContactEvents().size() == 8);
    CHECK(world.GetDroppedContactEventCount() >= 24);

    world.ClearContactEvents();
    CHECK(world.GetContactEvents().empty());
    CHECK(world.GetDroppedContactEventCount() == 0);

    world.DestroyShape(boxShape);
    world.DestroyShape(floorShape);
}

// ---------------------------------------------------------------------------
// 3. Multi-threaded ECS + Jolt stress: 500 dynamic bodies + sleeping + teardown
// ---------------------------------------------------------------------------
TEST_CASE("Physics Stress: 500 dynamic ECS entities across 90 FixedSteps with BlockingThreadPool(4)")
{
    StressPoolGuard poolGuard(4);

    PhysicsWorld world;
    auto cfg = PhysicsWorldConfig::Default();
    cfg.maxContactEvents = 16384;
    REQUIRE(world.Init(cfg));

    EcsRegistry reg;
    EcsCommandQueue q;
    PhysicsSyncSystem physSync(world);

    std::vector<SystemDescriptor> descs = {
        physSync.TeardownDescriptor(SchedulePhase::PrePhysics),
        physSync.Descriptor(),
        physSync.TeardownDescriptor(SchedulePhase::RenderSync)
    };
    const PhaseSchedule schedule = BuildPhaseSchedule(descs);

    const ShapeID floorShape = world.CreateBoxShape(Vec3{ 100.0f, 1.0f, 100.0f });
    const ShapeID bodyShape  = world.CreateSphereShape(0.35f);

    // Static ground
    Entity floorEnt = reg.CreateEntity();
    reg.AddComponent(floorEnt, TransformComponent{
        Vec3{ 0.0f, -1.0f, 0.0f }, Quat::Identity(), Vec3{ 1.0f, 1.0f, 1.0f }
    }, q);
    reg.AddComponent(floorEnt, RigidBodyComponent{ Vec3{}, 0.0f, Vec3{}, InvalidBodyID }, q);
    reg.AddComponent(floorEnt, ColliderComponent::FromShape(
        floorShape, MotionType::Static, PhysicsLayer::Static), q);

    constexpr uint32_t kNumDynamic = 500;
    std::vector<Entity> entities;
    entities.reserve(kNumDynamic);

    for (uint32_t i = 0; i < kNumDynamic; ++i)
    {
        const uint32_t gx = i % 25;
        const uint32_t gz = (i / 25) % 20;
        const uint32_t gy = i / 500;

        Entity e = reg.CreateEntity();
        entities.push_back(e);

        reg.AddComponent(e, TransformComponent{
            Vec3{
                (static_cast<float>(gx) - 12.0f) * 1.2f,
                2.0f + static_cast<float>(gy) * 1.5f + static_cast<float>(i % 5) * 0.25f,
                (static_cast<float>(gz) - 10.0f) * 1.2f
            },
            Quat::Identity(),
            Vec3{ 1.0f, 1.0f, 1.0f }
        }, q);
        reg.AddComponent(e, PreviousTransformComponent::FromTransform(TransformComponent::Identity()), q);
        reg.AddComponent(e, RigidBodyComponent{ Vec3{}, 1.0f, Vec3{}, InvalidBodyID }, q);
        reg.AddComponent(e, ColliderComponent::FromShape(
            bodyShape, MotionType::Dynamic, PhysicsLayer::Dynamic), q);
    }

    reg.FlushCommands(q);

    constexpr float kDt = 1.0f / 60.0f;
    for (int frame = 0; frame < 90; ++frame)
    {
        world.ClearContactEvents();
        DispatchPhase(schedule, SchedulePhase::PrePhysics, reg, q, BlockingThreadPool::Get(), kDt);
        reg.FlushCommands(q);
        DispatchPhase(schedule, SchedulePhase::FixedStep, reg, q, BlockingThreadPool::Get(), kDt);
        reg.FlushCommands(q);
        DispatchPhase(schedule, SchedulePhase::RenderSync, reg, q, BlockingThreadPool::Get(), kDt);
        reg.FlushCommands(q);
        reg.ClearTombstones();
    }

    CHECK(world.GetBodyCount() == kNumDynamic + 1);

    // All dynamic bodies must have finite, floor-bounded transforms
    const auto tfView = reg.Pool<TransformComponent>().ReadView();
    for (uint32_t i = 0; i < tfView.Count(); ++i)
    {
        const Vec3& p = tfView.data[i].position;
        CHECK(std::isfinite(p.x));
        CHECK(std::isfinite(p.y));
        CHECK(std::isfinite(p.z));
        CHECK(p.y >= -2.0f);
    }

    // Destroy all entities and verify 0 leaked Jolt bodies
    reg.DestroyEntity(floorEnt, q);
    for (Entity e : entities)
        reg.DestroyEntity(e, q);
    reg.FlushCommands(q);

    DispatchPhase(schedule, SchedulePhase::RenderSync, reg, q, BlockingThreadPool::Get(), kDt);
    reg.FlushCommands(q);
    reg.ClearTombstones();

    CHECK(world.GetBodyCount() == 0);

    world.DestroyShape(bodyShape);
    world.DestroyShape(floorShape);
}

// ---------------------------------------------------------------------------
// 4. Streaming geometry batch churn (CreateBodies/DestroyBodies + MeshShapes)
// ---------------------------------------------------------------------------
TEST_CASE("Physics Stress: Streaming chunk batch create/destroy and mesh shape churn")
{
    StressPoolGuard poolGuard(2);

    PhysicsWorld world;
    REQUIRE(world.Init(PhysicsWorldConfig::Default()));

    const ShapeID boxShape = world.CreateBoxShape(Vec3{ 0.5f, 0.5f, 0.5f });

    const std::array<Vec3, 4> chunkVerts = {
        Vec3{ -8.0f, 0.0f, -8.0f },
        Vec3{  8.0f, 0.0f, -8.0f },
        Vec3{  8.0f, 0.0f,  8.0f },
        Vec3{ -8.0f, 0.0f,  8.0f }
    };
    const std::array<uint32_t, 6> chunkIndices = { 0, 2, 1, 0, 3, 2 };

    constexpr uint32_t kBatch = 128;
    std::array<BodyDesc, kBatch> descs{};
    std::array<BodyID, kBatch>   ids{};

    for (uint32_t wave = 0; wave < 20; ++wave)
    {
        const ShapeID meshShape = world.CreateMeshShape(chunkVerts, chunkIndices);
        REQUIRE(meshShape.IsValid());

        descs[0].motion   = MotionType::Static;
        descs[0].shape    = meshShape;
        descs[0].layer    = PhysicsLayer::Static;
        descs[0].position = Vec3{ 0.0f, 0.0f, 0.0f };

        for (uint32_t i = 1; i < kBatch; ++i)
        {
            descs[i].motion   = MotionType::Dynamic;
            descs[i].shape    = boxShape;
            descs[i].layer    = PhysicsLayer::Dynamic;
            descs[i].position = Vec3{
                static_cast<float>(i % 16) - 8.0f,
                2.0f + static_cast<float>(i / 16) * 1.2f,
                0.0f
            };
        }

        REQUIRE(world.CreateBodies(descs, ids) == kBatch);
        CHECK(world.GetBodyCount() == kBatch);

        world.ClearContactEvents();
        world.Step(1.0f / 60.0f);

        world.DestroyBodies(ids);
        world.DestroyShape(meshShape);
        CHECK(world.GetBodyCount() == 0);
        CHECK_FALSE(world.IsShapeValid(meshShape));
    }

    world.DestroyShape(boxShape);
}

// ---------------------------------------------------------------------------
// 5. Concurrent multi-threaded read-only spatial queries
// ---------------------------------------------------------------------------
TEST_CASE("Physics Stress: Concurrent Raycast / RaycastAll / OverlapSphere from 4 threads")
{
    PhysicsWorld world;
    REQUIRE(world.Init(PhysicsWorldConfig::Default()));

    const ShapeID sphereShape = world.CreateSphereShape(0.4f);
    constexpr uint32_t kBodies = 200;
    std::array<BodyDesc, kBodies> descs{};
    std::array<BodyID, kBodies>   ids{};
    for (uint32_t i = 0; i < kBodies; ++i)
    {
        descs[i].motion   = MotionType::Static;
        descs[i].shape    = sphereShape;
        descs[i].layer    = PhysicsLayer::Static;
        descs[i].position = Vec3{ 0.0f, 0.0f, static_cast<float>(i) * 2.0f + 2.0f };
        descs[i].userData = Entity{ i + 1 };
    }
    REQUIRE(world.CreateBodies(descs, ids) == kBodies);
    world.OptimizeBroadPhase();

    std::atomic<uint32_t> failures{ 0 };
    std::vector<std::thread> threads;
    threads.reserve(4);
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&]() {
            std::array<HitResult, 8> hits{};
            std::array<BodyID, 8>    overlaps{};
            for (int iter = 0; iter < 250; ++iter)
            {
                HitResult single{};
                if (!world.Raycast(Vec3{ 0.0f, 0.0f, 0.0f }, Vec3{ 0.0f, 0.0f, 1.0f },
                                   500.0f, kAllLayersMask, single) ||
                    single.bodyId != ids[0])
                {
                    failures.fetch_add(1, std::memory_order_relaxed);
                }

                const uint32_t hitCount = world.RaycastAll(
                    Vec3{ 0.0f, 0.0f, 0.0f }, Vec3{ 0.0f, 0.0f, 1.0f },
                    500.0f, kAllLayersMask, hits);
                if (hitCount != 8 || hits[0].bodyId != ids[0])
                {
                    failures.fetch_add(1, std::memory_order_relaxed);
                }

                const uint32_t ovCount = world.OverlapSphere(
                    Vec3{ 0.0f, 0.0f, 4.0f }, 2.5f, kAllLayersMask, overlaps);
                if (ovCount < 2)
                {
                    failures.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& th : threads)
        th.join();

    CHECK(failures.load() == 0);

    world.DestroyBodies(ids);
    world.DestroyShape(sphereShape);
}
