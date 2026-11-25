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
#include <cmath>
#include <vector>

#if defined(JPH_NAMESPACE_BEGIN) || defined(JPH_VERSION_ID) || defined(JPH_DOUBLE_PRECISION)
    #error "Public physics headers leaked Jolt macros!"
#endif

using namespace Zero;

namespace
{
    bool ApproxEq(double a, double b, double eps = 1e-3)
    {
        return std::fabs(a - b) <= eps;
    }

    struct ScopedPoolInit
    {
        explicit ScopedPoolInit(uint32_t threads = 2)
        {
            BlockingThreadPool::Get().Init(threads);
        }
        ~ScopedPoolInit()
        {
            BlockingThreadPool::Get().Shutdown();
        }
    };
}

// ---------------------------------------------------------------------------
// 1. Dynamic body gravity fall & static floor collision + ContactEvents
// ---------------------------------------------------------------------------
TEST_CASE("Physics: Dynamic body falls under gravity and collides with static floor")
{
    ScopedPoolInit poolGuard(2);

    PhysicsWorld world;
    REQUIRE(world.Init(PhysicsWorldConfig::Default()));

    EcsRegistry reg;
    EcsCommandQueue q;
    PhysicsSyncSystem physSync(world);

    std::vector<SystemDescriptor> descs = {
        physSync.TeardownDescriptor(SchedulePhase::PrePhysics),
        physSync.Descriptor(),
        physSync.TeardownDescriptor(SchedulePhase::RenderSync)
    };
    const PhaseSchedule schedule = BuildPhaseSchedule(descs);

    // Static floor at y = -0.5, half-extents (20, 0.5, 20) -> top surface at y = 0.0
    Entity floorEnt = reg.CreateEntity();
    reg.AddComponent(floorEnt, TransformComponent{
        RVec3{ 0.0f, -0.5f, 0.0f }, Quat::Identity(), Vec3{ 1.0f, 1.0f, 1.0f }
    }, q);
    reg.AddComponent(floorEnt, RigidBodyComponent{
        Vec3{ 0.0f, 0.0f, 0.0f }, 0.0f, Vec3{ 0.0f, 0.0f, 0.0f }, InvalidBodyID
    }, q);
    reg.AddComponent(floorEnt, ColliderComponent::Box(
        Vec3{ 20.0f, 0.5f, 20.0f }, MotionType::Static, PhysicsLayer::Static
    ), q);

    // Dynamic box falling from y = 3.0, half-extents (0.5, 0.5, 0.5) -> rests around y = 0.5
    Entity boxEnt = reg.CreateEntity();
    reg.AddComponent(boxEnt, TransformComponent{
        RVec3{ 0.0f, 3.0f, 0.0f }, Quat::Identity(), Vec3{ 1.0f, 1.0f, 1.0f }
    }, q);
    reg.AddComponent(boxEnt, RigidBodyComponent{
        Vec3{ 0.0f, 0.0f, 0.0f }, 1.0f, Vec3{ 0.0f, 0.0f, 0.0f }, InvalidBodyID
    }, q);
    reg.AddComponent(boxEnt, ColliderComponent::Box(
        Vec3{ 0.5f, 0.5f, 0.5f }, MotionType::Dynamic, PhysicsLayer::Dynamic
    ), q);

    reg.FlushCommands(q);

    constexpr float kDt = 1.0f / 60.0f;
    bool sawContactEvent = false;
    for (int step = 0; step < 120; ++step)
    {
        DispatchPhase(schedule, SchedulePhase::FixedStep, reg, q, BlockingThreadPool::Get(), kDt);
        reg.FlushCommands(q);
        for (const auto& ev : world.GetContactEvents())
        {
            if ((ev.entityA == boxEnt && ev.entityB == floorEnt) ||
                (ev.entityA == floorEnt && ev.entityB == boxEnt))
            {
                sawContactEvent = true;
            }
        }
    }

    const TransformComponent* boxTf = reg.Pool<TransformComponent>().Get(Index(boxEnt));
    const RigidBodyComponent* boxRb = reg.Pool<RigidBodyComponent>().Get(Index(boxEnt));
    REQUIRE(boxTf != nullptr);
    REQUIRE(boxRb != nullptr);
    CHECK(boxRb->bodyId.IsValid());
    CHECK(sawContactEvent);
    // Resting height should be near y = 0.5 (box half-extent above y=0 floor)
    CHECK(boxTf->position.y >= 0.45f);
    CHECK(boxTf->position.y <= 0.56f);
    CHECK(std::fabs(boxRb->linearVelocity.y) < 0.2f);
}

// ---------------------------------------------------------------------------
// 2. Kinematic body movement & collision response
// ---------------------------------------------------------------------------
TEST_CASE("Physics: Kinematic body moves via Gameplay transform and pushes dynamic body")
{
    ScopedPoolInit poolGuard(2);

    PhysicsWorld world;
    REQUIRE(world.Init(PhysicsWorldConfig::Default()));

    EcsRegistry reg;
    EcsCommandQueue q;
    PhysicsSyncSystem physSync(world);

    std::vector<SystemDescriptor> descs = { physSync.Descriptor() };
    const PhaseSchedule schedule = BuildPhaseSchedule(descs);

    // Kinematic pusher starting at x = -2.0
    Entity kinEnt = reg.CreateEntity();
    reg.AddComponent(kinEnt, TransformComponent{
        RVec3{ -2.0f, 0.5f, 0.0f }, Quat::Identity(), Vec3{ 1.0f, 1.0f, 1.0f }
    }, q);
    reg.AddComponent(kinEnt, RigidBodyComponent{
        Vec3{ 0.0f, 0.0f, 0.0f }, 0.0f, Vec3{ 0.0f, 0.0f, 0.0f }, InvalidBodyID
    }, q);
    reg.AddComponent(kinEnt, ColliderComponent::Box(
        Vec3{ 0.5f, 0.5f, 0.5f }, MotionType::Kinematic, PhysicsLayer::Kinematic
    ), q);

    // Dynamic target at x = 0.0 (zero gravity so it sits in place until struck)
    Entity dynEnt = reg.CreateEntity();
    reg.AddComponent(dynEnt, TransformComponent{
        RVec3{ 0.0f, 0.5f, 0.0f }, Quat::Identity(), Vec3{ 1.0f, 1.0f, 1.0f }
    }, q);
    reg.AddComponent(dynEnt, RigidBodyComponent{
        Vec3{ 0.0f, 0.0f, 0.0f }, 1.0f, Vec3{ 0.0f, 0.0f, 0.0f }, InvalidBodyID
    }, q);
    auto dynCol = ColliderComponent::Box(
        Vec3{ 0.5f, 0.5f, 0.5f }, MotionType::Dynamic, PhysicsLayer::Dynamic);
    dynCol.gravityFactor = 0.0f;
    reg.AddComponent(dynEnt, dynCol, q);

    reg.FlushCommands(q);

    constexpr float kDt = 1.0f / 60.0f;
    // Step 1 creates the Jolt bodies
    DispatchPhase(schedule, SchedulePhase::FixedStep, reg, q, BlockingThreadPool::Get(), kDt);
    reg.FlushCommands(q);

    // Drive kinematic body in +X across the dynamic box
    for (int step = 0; step < 30; ++step)
    {
        const uint64_t gpEpoch = reg.AdvanceEpoch();
        {
            auto tfRef = reg.Pool<TransformComponent>().GetMutable(Index(kinEnt), gpEpoch, WriteSource::Gameplay);
            tfRef->position.x += 0.15f;
        }
        DispatchPhase(schedule, SchedulePhase::FixedStep, reg, q, BlockingThreadPool::Get(), kDt);
        reg.FlushCommands(q);
    }

    const TransformComponent* dynTf = reg.Pool<TransformComponent>().Get(Index(dynEnt));
    REQUIRE(dynTf != nullptr);
    CHECK(dynTf->position.x > 0.5f);
}

// ---------------------------------------------------------------------------
// 3. Gameplay teleport updates Jolt without feedback loop on subsequent substeps
// ---------------------------------------------------------------------------
TEST_CASE("Physics: Gameplay teleport pushes to Jolt once with no dirty feedback loop")
{
    ScopedPoolInit poolGuard(2);

    PhysicsWorld world;
    auto cfg = PhysicsWorldConfig::Default();
    cfg.gravity = Vec3{ 0.0f, 0.0f, 0.0f };
    REQUIRE(world.Init(cfg));

    EcsRegistry reg;
    EcsCommandQueue q;
    PhysicsSyncSystem physSync(world);

    Entity e = reg.CreateEntity();
    reg.AddComponent(e, TransformComponent{
        RVec3{ 0.0f, 0.0f, 0.0f }, Quat::Identity(), Vec3{ 1.0f, 1.0f, 1.0f }
    }, q);
    reg.AddComponent(e, RigidBodyComponent{
        Vec3{ 1.0f, 0.0f, 0.0f }, 1.0f, Vec3{ 0.0f, 0.0f, 0.0f }, InvalidBodyID
    }, q);
    reg.AddComponent(e, ColliderComponent::Sphere(0.5f, MotionType::Dynamic, PhysicsLayer::Dynamic), q);
    reg.FlushCommands(q);

    constexpr float kDt = 1.0f / 60.0f;

    // Step 1: create body and advance once
    physSync.Execute(reg, q, kDt, reg.AdvanceEpoch());
    const BodyID bodyId = reg.Pool<RigidBodyComponent>().Get(Index(e))->bodyId;
    REQUIRE(bodyId.IsValid());

    // Gameplay teleports entity to x = 100.0
    {
        const uint64_t gpEpoch = reg.AdvanceEpoch();
        auto tfRef = reg.Pool<TransformComponent>().GetMutable(Index(e), gpEpoch, WriteSource::Gameplay);
        tfRef->position = RVec3{ 100.0f, 5.0f, 0.0f };
    }

    // Substep 1: Gameplay teleport is pushed to Jolt; body simulates forward from 100.0
    physSync.Execute(reg, q, kDt, reg.AdvanceEpoch());
    const Real posAfterSubstep1 = world.GetPosition(bodyId).x;
    CHECK(posAfterSubstep1 > Real(100.0));
    CHECK(reg.Pool<TransformComponent>().ModifiedSourceOf(Index(e)) == WriteSource::Physics);

    // Substep 2: No gameplay write happened. TransformComponent was written by Physics,
    // so PhysicsSyncSystem must NOT re-teleport the body back to posAfterSubstep1.
    const uint64_t prevSyncedEpoch = physSync.LastSyncedEpoch();
    CHECK(reg.Pool<TransformComponent>().ModifiedEpochOf(Index(e)) <= prevSyncedEpoch);

    physSync.Execute(reg, q, kDt, reg.AdvanceEpoch());
    const Real posAfterSubstep2 = world.GetPosition(bodyId).x;
    CHECK(posAfterSubstep2 > posAfterSubstep1);
    CHECK(ApproxEq(reg.Pool<TransformComponent>().Get(Index(e))->position.x, posAfterSubstep2, 1e-4));
}

// ---------------------------------------------------------------------------
// 4. DestroyEntity() and RemoveComponent<RigidBodyComponent>() teardown
// ---------------------------------------------------------------------------
TEST_CASE("Physics: DestroyEntity and RemoveComponent drain tombstones cleanly (GetBodyCount == 0)")
{
    ScopedPoolInit poolGuard(2);

    PhysicsWorld world;
    REQUIRE(world.Init(PhysicsWorldConfig::Default()));

    EcsRegistry reg;
    EcsCommandQueue q;
    PhysicsSyncSystem physSync(world);

    std::vector<SystemDescriptor> descs = {
        physSync.TeardownDescriptor(SchedulePhase::PrePhysics),
        physSync.Descriptor(),
        physSync.TeardownDescriptor(SchedulePhase::RenderSync)
    };
    const PhaseSchedule schedule = BuildPhaseSchedule(descs);

    Entity eDestroy = reg.CreateEntity();
    Entity eStrip   = reg.CreateEntity();

    for (Entity e : { eDestroy, eStrip })
    {
        reg.AddComponent(e, TransformComponent::Identity(), q);
        reg.AddComponent(e, RigidBodyComponent{
            Vec3{}, 1.0f, Vec3{}, InvalidBodyID
        }, q);
    }
    reg.FlushCommands(q);

    // Create bodies in FixedStep
    DispatchPhase(schedule, SchedulePhase::FixedStep, reg, q, BlockingThreadPool::Get(), 1.0f / 60.0f);
    reg.FlushCommands(q);
    reg.ClearTombstones();
    CHECK(world.GetBodyCount() == 2);

    // Destroy eDestroy & strip RigidBodyComponent from eStrip
    reg.DestroyEntity(eDestroy, q);
    reg.RemoveComponent<RigidBodyComponent>(eStrip, q);
    reg.FlushCommands(q);

    // Even on a 0-substep frame (only PrePhysics + RenderSync run, FixedStep skipped),
    // tombstones must be drained before ClearTombstones() clears them!
    DispatchPhase(schedule, SchedulePhase::PrePhysics, reg, q, BlockingThreadPool::Get(), 0.004f);
    reg.FlushCommands(q);
    DispatchPhase(schedule, SchedulePhase::RenderSync, reg, q, BlockingThreadPool::Get(), 0.004f);
    reg.FlushCommands(q);
    reg.ClearTombstones();

    CHECK(world.GetBodyCount() == 0);
}

// ---------------------------------------------------------------------------
// 5. Zero-allocation Raycast, RaycastAll, OverlapSphere, OverlapBox
// ---------------------------------------------------------------------------
TEST_CASE("Physics: Zero-allocation Raycast, RaycastAll, OverlapSphere, and OverlapBox")
{
    PhysicsWorld world;
    REQUIRE(world.Init(PhysicsWorldConfig::Default()));

    const ShapeID boxShape = world.CreateBoxShape(Vec3{ 0.5f, 0.5f, 0.5f });
    REQUIRE(boxShape.IsValid());

    BodyDesc d1;
    d1.motion   = MotionType::Static;
    d1.shape    = boxShape;
    d1.position = RVec3{ 0.0f, 0.0f, 5.0f };
    d1.layer    = PhysicsLayer::Static;
    d1.userData = Entity{ 1001 };

    BodyDesc d2 = d1;
    d2.position = RVec3{ 0.0f, 0.0f, 10.0f };
    d2.userData = Entity{ 1002 };

    BodyDesc d3 = d1;
    d3.motion   = MotionType::Dynamic;
    d3.layer    = PhysicsLayer::Dynamic;
    d3.position = RVec3{ 0.0f, 0.0f, 15.0f };
    d3.userData = Entity{ 1003 };

    const BodyID b1 = world.CreateBody(d1);
    const BodyID b2 = world.CreateBody(d2);
    const BodyID b3 = world.CreateBody(d3);
    world.OptimizeBroadPhase();

    // Raycast single closest hit
    HitResult hit{};
    const bool didHit = world.Raycast(
        RVec3{ 0.0f, 0.0f, 0.0f },
        Vec3{ 0.0f, 0.0f, 1.0f },
        50.0f,
        kAllLayersMask,
        hit);
    REQUIRE(didHit);
    CHECK(hit.bodyId == b1);
    CHECK(hit.entity == Entity{ 1001 });
    CHECK(ApproxEq(hit.distance, 4.5f, 1e-2f));

    // RaycastAll into stack-allocated array span (all layers)
    std::array<HitResult, 8> allHits{};
    const uint32_t allCount = world.RaycastAll(
        RVec3{ 0.0f, 0.0f, 0.0f },
        Vec3{ 0.0f, 0.0f, 1.0f },
        50.0f,
        kAllLayersMask,
        allHits);
    REQUIRE(allCount == 3);
    CHECK(allHits[0].bodyId == b1);
    CHECK(allHits[1].bodyId == b2);
    CHECK(allHits[2].bodyId == b3);

    // RaycastAll filtered to Dynamic layer only
    const uint32_t dynCount = world.RaycastAll(
        RVec3{ 0.0f, 0.0f, 0.0f },
        Vec3{ 0.0f, 0.0f, 1.0f },
        50.0f,
        MakeMask(PhysicsLayer::Dynamic),
        allHits);
    REQUIRE(dynCount == 1);
    CHECK(allHits[0].bodyId == b3);

    // OverlapSphere into stack span
    std::array<BodyID, 8> overlapBuf{};
    const uint32_t sphereCount = world.OverlapSphere(
        RVec3{ 0.0f, 0.0f, 7.5f },
        3.5f,
        kAllLayersMask,
        overlapBuf);
    CHECK(sphereCount == 2);

    // OverlapBox into stack span
    const uint32_t boxCount = world.OverlapBox(
        RVec3{ 0.0f, 0.0f, 15.0f },
        Vec3{ 1.0f, 1.0f, 1.0f },
        Quat::Identity(),
        MakeMask(PhysicsLayer::Dynamic),
        overlapBuf);
    REQUIRE(boxCount == 1);
    CHECK(overlapBuf[0] == b3);

    world.DestroyShape(boxShape);
}

// ---------------------------------------------------------------------------
// 6. PreviousTransformComponent interpolation across fixed substeps
// ---------------------------------------------------------------------------
TEST_CASE("Physics: PreviousTransformComponent snapshots before each FixedStep for interpolation")
{
    ScopedPoolInit poolGuard(2);

    PhysicsWorld world;
    auto cfg = PhysicsWorldConfig::Default();
    cfg.gravity = Vec3{ 0.0f, 0.0f, 0.0f };
    REQUIRE(world.Init(cfg));

    EcsRegistry reg;
    EcsCommandQueue q;
    PhysicsSyncSystem physSync(world);

    Entity e = reg.CreateEntity();
    reg.AddComponent(e, TransformComponent{
        RVec3{ 0.0f, 0.0f, 0.0f }, Quat::Identity(), Vec3{ 1.0f, 1.0f, 1.0f }
    }, q);
    reg.AddComponent(e, PreviousTransformComponent{
        RVec3{ 0.0f, 0.0f, 0.0f }, Quat::Identity(), Vec3{ 1.0f, 1.0f, 1.0f }
    }, q);
    // Moving at +6.0 units/sec in X (0.1 units per 1/60s step)
    reg.AddComponent(e, RigidBodyComponent{
        Vec3{ 6.0f, 0.0f, 0.0f }, 1.0f, Vec3{ 0.0f, 0.0f, 0.0f }, InvalidBodyID
    }, q);
    auto col = ColliderComponent::Sphere(0.5f, MotionType::Dynamic, PhysicsLayer::Dynamic);
    col.linearDamping = 0.0f;
    reg.AddComponent(e, col, q);
    reg.FlushCommands(q);

    constexpr float kDt = 1.0f / 60.0f;

    // Substep 1: prev = 0.0, curr ~= 0.1
    physSync.Execute(reg, q, kDt, reg.AdvanceEpoch());
    const Real prev1 = reg.Pool<PreviousTransformComponent>().Get(Index(e))->position.x;
    const Real curr1 = reg.Pool<TransformComponent>().Get(Index(e))->position.x;
    CHECK(ApproxEq(prev1, 0.0, 1e-4));
    CHECK(ApproxEq(curr1, 0.1, 1e-3));

    // Substep 2: prev == curr1, curr2 ~= 0.2
    physSync.Execute(reg, q, kDt, reg.AdvanceEpoch());
    const auto* prev2 = reg.Pool<PreviousTransformComponent>().Get(Index(e));
    const auto* curr2 = reg.Pool<TransformComponent>().Get(Index(e));
    CHECK(ApproxEq(prev2->position.x, curr1, 1e-4));
    CHECK(ApproxEq(curr2->position.x, 0.2, 1e-3));

    // Alpha = 0.5 interpolation produces midpoint ~= 0.15
    const TransformComponent mid = prev2->InterpolateTo(*curr2, 0.5f);
    CHECK(ApproxEq(mid.position.x, 0.15, 1e-3));
}

// ---------------------------------------------------------------------------
// 7. Batch CreateBodies/DestroyBodies, Mesh/Compound shapes, and Triggers
// ---------------------------------------------------------------------------
TEST_CASE("Physics: Batch streaming geometry, mesh/compound shapes, and trigger sensors")
{
    PhysicsWorld world;
    REQUIRE(world.Init(PhysicsWorldConfig::Default()));

    // Static triangle mesh floor (two triangles covering [-10, 10] in XZ at y = 0)
    const std::array<Vec3, 4> meshVerts = {
        Vec3{ -10.0f, 0.0f, -10.0f },
        Vec3{  10.0f, 0.0f, -10.0f },
        Vec3{  10.0f, 0.0f,  10.0f },
        Vec3{ -10.0f, 0.0f,  10.0f }
    };
    const std::array<uint32_t, 6> meshIndices = { 0, 2, 1, 0, 3, 2 };
    const ShapeID meshShape = world.CreateMeshShape(meshVerts, meshIndices);
    REQUIRE(meshShape.IsValid());

    // Compound shape built from two spheres
    const ShapeID sA = world.CreateSphereShape(0.4f);
    const std::array<CompoundSubShape, 2> subs = {
        CompoundSubShape{ sA, Vec3{ -0.5f, 0.0f, 0.0f }, Quat::Identity() },
        CompoundSubShape{ sA, Vec3{  0.5f, 0.0f, 0.0f }, Quat::Identity() }
    };
    const ShapeID compShape = world.CreateCompoundShape(subs);
    REQUIRE(compShape.IsValid());

    // Batch create mesh floor + sensor trigger + dynamic compound
    std::array<BodyDesc, 3> descs{};
    descs[0].motion   = MotionType::Static;
    descs[0].shape    = meshShape;
    descs[0].layer    = PhysicsLayer::Static;
    descs[0].userData = Entity{ 1 };

    descs[1].motion   = MotionType::Static;
    descs[1].shape    = sA;
    descs[1].position = RVec3{ 0.0f, 1.5f, 0.0f };
    descs[1].layer    = PhysicsLayer::Trigger;
    descs[1].isSensor = true;
    descs[1].userData = Entity{ 2 };

    descs[2].motion   = MotionType::Dynamic;
    descs[2].shape    = compShape;
    descs[2].position = RVec3{ 0.0f, 3.0f, 0.0f };
    descs[2].layer    = PhysicsLayer::Dynamic;
    descs[2].userData = Entity{ 3 };

    std::array<BodyID, 3> ids{};
    const uint32_t created = world.CreateBodies(descs, ids);
    REQUIRE(created == 3);
    CHECK(world.GetBodyCount() == 3);

    bool sawTriggerEnter = false;
    bool sawMeshContact  = false;
    for (int i = 0; i < 90; ++i)
    {
        world.Step(1.0f / 60.0f);
        for (const auto& ev : world.GetContactEvents())
        {
            if (ev.type == ContactType::TriggerEnter)
                sawTriggerEnter = true;
            if ((ev.entityA == Entity{ 1 } || ev.entityB == Entity{ 1 }) &&
                (ev.entityA == Entity{ 3 } || ev.entityB == Entity{ 3 }))
                sawMeshContact = true;
        }
    }

    CHECK(sawTriggerEnter);
    CHECK(sawMeshContact);
    CHECK(world.GetPosition(ids[2]).y > 0.0f);

    world.DestroyBodies(ids);
    CHECK(world.GetBodyCount() == 0);

    world.DestroyShape(compShape);
    world.DestroyShape(sA);
    world.DestroyShape(meshShape);
    CHECK_FALSE(world.IsShapeValid(meshShape));
}

#ifdef ZERO_DOUBLE_PRECISION
// ---------------------------------------------------------------------------
// 8. Large-world 64-bit precision at 100 km (100,000.0 m) from origin
// ---------------------------------------------------------------------------
TEST_CASE("Physics (Double Precision): Sub-millimeter resting contact and Raycast at 100 km")
{
    ScopedPoolInit poolGuard(2);

    PhysicsWorld world;
    REQUIRE(world.Init(PhysicsWorldConfig::Default()));

    EcsRegistry reg;
    EcsCommandQueue q;
    PhysicsSyncSystem physSync(world);

    constexpr double kOrigin = 100'000.0;

    // Static floor centered at (100000, 99999.5, 100000), half-extents (20, 0.5, 20)
    // -> top surface at exact y = 100000.0 m
    Entity floorEnt = reg.CreateEntity();
    reg.AddComponent(floorEnt, TransformComponent{
        DVec3{ kOrigin, kOrigin - 0.5, kOrigin }, Quat::Identity(), Vec3{ 1.0f, 1.0f, 1.0f }
    }, q);
    reg.AddComponent(floorEnt, RigidBodyComponent{
        Vec3{ 0.0f, 0.0f, 0.0f }, 0.0f, Vec3{ 0.0f, 0.0f, 0.0f }, InvalidBodyID
    }, q);
    reg.AddComponent(floorEnt, ColliderComponent::Box(
        Vec3{ 20.0f, 0.5f, 20.0f }, MotionType::Static, PhysicsLayer::Static
    ), q);

    // Dynamic box half-extents (0.5, 0.5, 0.5) falling from y = 100002.0 -> rests at y = 100000.5
    Entity boxEnt = reg.CreateEntity();
    reg.AddComponent(boxEnt, TransformComponent{
        DVec3{ kOrigin, kOrigin + 2.0, kOrigin }, Quat::Identity(), Vec3{ 1.0f, 1.0f, 1.0f }
    }, q);
    reg.AddComponent(boxEnt, RigidBodyComponent{
        Vec3{ 0.0f, 0.0f, 0.0f }, 1.0f, Vec3{ 0.0f, 0.0f, 0.0f }, InvalidBodyID
    }, q);
    reg.AddComponent(boxEnt, ColliderComponent::Box(
        Vec3{ 0.5f, 0.5f, 0.5f }, MotionType::Dynamic, PhysicsLayer::Dynamic
    ), q);

    reg.FlushCommands(q);

    constexpr float kDt = 1.0f / 60.0f;
    bool sawContact = false;
    DVec3 contactPoint{};
    for (int step = 0; step < 180; ++step)
    {
        world.ClearContactEvents();
        physSync.Execute(reg, q, kDt, reg.AdvanceEpoch());
        reg.FlushCommands(q);

        for (const auto& ev : world.GetContactEvents())
        {
            if ((ev.entityA == boxEnt && ev.entityB == floorEnt) ||
                (ev.entityA == floorEnt && ev.entityB == boxEnt))
            {
                if (ev.type == ContactType::Added || ev.type == ContactType::Persisted)
                {
                    sawContact   = true;
                    contactPoint = ev.point;
                }
            }
        }
    }

    const TransformComponent* boxTf = reg.Pool<TransformComponent>().Get(Index(boxEnt));
    const RigidBodyComponent* boxRb = reg.Pool<RigidBodyComponent>().Get(Index(boxEnt));
    REQUIRE(boxTf != nullptr);
    REQUIRE(boxRb != nullptr);
    REQUIRE(boxRb->bodyId.IsValid());

    CHECK(sawContact);
    // Sub-millimeter resting contact point accuracy at 100 km
    CHECK(ApproxEq(contactPoint.y, kOrigin, 1e-3));

    // Compare resting position at 100 km against an identical origin-local simulation
    // (Jolt's default mPenetrationSlop is 0.02 m, so resting y differs slightly from 0.5,
    //  but 64-bit world precision matches origin-local resting y to sub-millimeter!)
    PhysicsWorld refWorld;
    REQUIRE(refWorld.Init(PhysicsWorldConfig::Default()));
    const ShapeID refFloorShape = refWorld.CreateBoxShape(Vec3{ 20.0f, 0.5f, 20.0f });
    const ShapeID refBoxShape   = refWorld.CreateBoxShape(Vec3{ 0.5f, 0.5f, 0.5f });
    BodyDesc refFloor{};
    refFloor.motion   = MotionType::Static;
    refFloor.shape    = refFloorShape;
    refFloor.position = DVec3{ 0.0, -0.5, 0.0 };
    refFloor.layer    = PhysicsLayer::Static;
    (void)refWorld.CreateBody(refFloor);
    BodyDesc refBox{};
    refBox.motion   = MotionType::Dynamic;
    refBox.shape    = refBoxShape;
    refBox.position = DVec3{ 0.0, 2.0, 0.0 };
    refBox.layer    = PhysicsLayer::Dynamic;
    const BodyID refBoxId = refWorld.CreateBody(refBox);
    for (int step = 0; step < 180; ++step)
    {
        refWorld.ClearContactEvents();
        refWorld.Step(kDt, 1);
    }
    const DVec3 refPos = refWorld.GetPosition(refBoxId);

    const DVec3 worldPos = world.GetPosition(boxRb->bodyId);
    CHECK(ApproxEq(worldPos.x, kOrigin, 1e-4));
    CHECK(ApproxEq(worldPos.z, kOrigin, 1e-4));
    CHECK(ApproxEq(worldPos.y - kOrigin, refPos.y, 1e-4));
    CHECK(ApproxEq(boxTf->position.x, worldPos.x, 1e-6));
    CHECK(ApproxEq(boxTf->position.y, worldPos.y, 1e-6));
    CHECK(ApproxEq(boxTf->position.z, worldPos.z, 1e-6));
    CHECK(std::fabs(boxRb->linearVelocity.y) < 1e-2f);

    // Raycast from 10 m above down onto the resting box's top face (y = worldPos.y + 0.5)
    HitResult hit{};
    const bool didHit = world.Raycast(
        DVec3{ kOrigin, kOrigin + 10.0, kOrigin },
        Vec3{ 0.0f, -1.0f, 0.0f },
        20.0f,
        MakeMask(PhysicsLayer::Dynamic),
        hit);
    REQUIRE(didHit);
    CHECK(hit.entity == boxEnt);
    CHECK(ApproxEq(hit.position.x, kOrigin, 1e-4));
    CHECK(ApproxEq(hit.position.z, kOrigin, 1e-4));
    CHECK(ApproxEq(hit.position.y, worldPos.y + 0.5, 1e-3));
}
#endif
