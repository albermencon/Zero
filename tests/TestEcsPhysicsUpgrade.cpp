#include <doctest.h>
#include <Engine/Ecs/EcsRegistry.h>
#include <Engine/Ecs/EcsCommandQueue.h>
#include <Engine/Ecs/Scheduler.h>
#include <Engine/Ecs/Systems/RenderSyncSystem.h>
#include <Engine/Ecs/Components/TransformComponent.h>
#include <Engine/Ecs/Components/PreviousTransformComponent.h>
#include <Engine/Ecs/Components/RigidBodyComponent.h>
#include <Engine/Ecs/Components/MeshComponent.h>
#include <Engine/Ecs/Components/BoundsComponent.h>
#include <Engine/Ecs/Components/RenderableHandleComponent.h>
#include <Engine/Ecs/Components/VelocityComponent.h>
#include <Engine/Ecs/Components/MaterialComponent.h>
#include <Engine/Ecs/Components/TagComponent.h>
#include <Engine/Ecs/Components/NameComponent.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/Scene.h>

#include "JobSystem/BlockingThreadPool.h"

#include <atomic>
#include <cmath>
#include <numbers>
#include <thread>
#include <vector>

using namespace Zero;

namespace
{
    MeshResolverFn MakeDummyMeshResolver()
    {
        return [](uint32_t, uint32_t, MeshResolvedDesc& out) {
            out.vertexBuffer  = {};
            out.indexBuffer   = {};
            out.pipeline      = {};
            out.indexCount    = 36;
            out.firstIndex    = 0;
            out.vertexOffset  = 0;
            out.pipelineId    = 1;
            out.materialId    = 1;
            out.castsShadow   = true;
            out.isTransparent = false;
            return true;
        };
    }

    bool ApproxEq(float a, float b, float eps = 1e-4f)
    {
        return std::fabs(a - b) <= eps;
    }

    bool ApproxVec3(const Vec3& a, const Vec3& b, float eps = 1e-4f)
    {
        return ApproxEq(a.x, b.x, eps) && ApproxEq(a.y, b.y, eps) && ApproxEq(a.z, b.z, eps);
    }

    bool ApproxMat4(const Mat4& a, const Mat4& b, float eps = 1e-4f)
    {
        for (int i = 0; i < 16; ++i)
        {
            if (!ApproxEq(a.m[i], b.m[i], eps)) return false;
        }
        return true;
    }
}

// ---------------------------------------------------------------------------
// 1. Lifecycle Tombstones — RemoveComponent & DestroyEntity capture handles
//    before swap-and-pop and accumulate across flushes until ClearTombstones()
// ---------------------------------------------------------------------------
TEST_CASE("ECS Pass 2: Per-pool tombstones capture removed handles on RemoveComponent and DestroyEntity")
{
    EcsRegistry reg;
    EcsCommandQueue q;

    Entity e1 = reg.CreateEntity();
    Entity e2 = reg.CreateEntity();
    Entity e3 = reg.CreateEntity();

    RigidBodyComponent rb1{};
    rb1.bodyId = BodyID{ 101 };
    rb1.invMass = 1.0f;

    RigidBodyComponent rb2{};
    rb2.bodyId = BodyID{ 202 };
    rb2.invMass = 0.5f;

    RigidBodyComponent rb3{};
    rb3.bodyId = BodyID{ 303 };
    rb3.invMass = 0.25f;

    reg.AddComponent(e1, rb1, q);
    reg.AddComponent(e2, rb2, q);
    reg.AddComponent(e3, rb3, q);
    reg.AddComponent(e2, RenderableHandleComponent{ 77 }, q);
    reg.FlushCommands(q);

    // Flush 1: explicit component removal on e1
    reg.RemoveComponent<RigidBodyComponent>(e1, q);
    reg.FlushCommands(q);

    // Flush 2: full entity destruction on e2 (must accumulate, not overwrite Flush 1)
    reg.DestroyEntity(e2, q);
    reg.FlushCommands(q);

    // Verify RigidBodyComponent tombstones contain BOTH e1 and e2 with exact handles
    const auto rbRemoved = reg.Pool<RigidBodyComponent>().Removed();
    REQUIRE(rbRemoved.size() == 2);
    CHECK(rbRemoved[0].entity == e1);
    CHECK(rbRemoved[0].value.bodyId == BodyID{ 101 });
    CHECK(rbRemoved[1].entity == e2);
    CHECK(rbRemoved[1].value.bodyId == BodyID{ 202 });

    // Verify RenderableHandleComponent tombstone captured e2's handle before FinalizeDestroy
    const auto rhRemoved = reg.Pool<RenderableHandleComponent>().Removed();
    REQUIRE(rhRemoved.size() == 1);
    CHECK(rhRemoved[0].entity == e2);
    CHECK(rhRemoved[0].value.id == 77u);

    // Verify registry-level DestroyedEntities recorded e2 (and NOT e1)
    const auto destroyed = reg.DestroyedEntities();
    REQUIRE(destroyed.size() == 1);
    CHECK(destroyed[0] == e2);

    // e3 survived swap-and-pop intact
    const RigidBodyComponent* aliveRb = reg.Pool<RigidBodyComponent>().Get(Index(e3));
    REQUIRE(aliveRb != nullptr);
    CHECK(aliveRb->bodyId == BodyID{ 303 });

    // End-of-frame ClearTombstones drains all buffers while preserving capacity
    const size_t rbCapBefore = reg.Pool<RigidBodyComponent>().removedEntities.capacity();
    reg.ClearTombstones();
    CHECK(reg.Pool<RigidBodyComponent>().Removed().empty());
    CHECK(reg.Pool<RenderableHandleComponent>().Removed().empty());
    CHECK(reg.DestroyedEntities().empty());
    CHECK(reg.Pool<RigidBodyComponent>().removedEntities.capacity() == rbCapBefore);
}

// ---------------------------------------------------------------------------
// 1b. RenderSyncSystem — Destroyed entities and mesh-removed entities do NOT
//     leak Scene renderables.
// ---------------------------------------------------------------------------
TEST_CASE("ECS Pass 2: RenderSyncSystem drains tombstones and leaks zero Scene renderables")
{
    SceneManager::Get().Init();
    const SceneId sceneId = SceneManager::Get().CreateScene("TombstoneTestScene", RenderPath::CPUDriven);
    Scene* scene = SceneManager::Get().GetScene(sceneId);
    REQUIRE(scene != nullptr);

    EcsRegistry reg;
    EcsCommandQueue q;
    RenderSyncSystem sync(*scene, MakeDummyMeshResolver());

    Entity eDestroy = reg.CreateEntity();
    Entity eStrip   = reg.CreateEntity();
    Entity eKeep    = reg.CreateEntity();

    for (Entity e : { eDestroy, eStrip, eKeep })
    {
        reg.AddComponent(e, MeshComponent{ 1, 1 }, q);
        reg.AddComponent(e, TransformComponent::Identity(), q);
    }
    reg.FlushCommands(q);

    // Frame 1: RenderSync creates 3 Scene renderables
    sync.Execute(reg, q, 0.016f, reg.AdvanceEpoch());
    reg.FlushCommands(q);
    reg.ClearTombstones();
    SceneManager::Get().FlushCommands();

    CHECK(scene->GetRenderableCount() == 3);
    CHECK(reg.Pool<RenderableHandleComponent>().Count() == 3);

    // Frame 2: Destroy eDestroy AND strip MeshComponent from eStrip
    reg.DestroyEntity(eDestroy, q);
    reg.RemoveComponent<MeshComponent>(eStrip, q);
    reg.FlushCommands(q);

    sync.Execute(reg, q, 0.016f, reg.AdvanceEpoch());
    reg.FlushCommands(q);
    reg.ClearTombstones();
    SceneManager::Get().FlushCommands();

    // Only eKeep's renderable should remain in Scene!
    CHECK(scene->GetRenderableCount() == 1);
    CHECK(reg.Pool<RenderableHandleComponent>().Count() == 1);
    CHECK(reg.Pool<RenderableHandleComponent>().Has(Index(eKeep)) == true);

    // Frame 3: Destroy eKeep — Scene renderable count returns to 0
    reg.DestroyEntity(eKeep, q);
    reg.FlushCommands(q);

    sync.Execute(reg, q, 0.016f, reg.AdvanceEpoch());
    reg.FlushCommands(q);
    reg.ClearTombstones();
    SceneManager::Get().FlushCommands();

    CHECK(scene->GetRenderableCount() == 0);
    CHECK(reg.Pool<RenderableHandleComponent>().Count() == 0);

    SceneManager::Get().DestroyScene(scene->GetId());
}

// ---------------------------------------------------------------------------
// 2a. TransformComponent TRS ToMat4 & PreviousTransformComponent interpolation
// ---------------------------------------------------------------------------
TEST_CASE("ECS Pass 2: TransformComponent TRS composition and fixed-step interpolation")
{
    const Vec3 pos{ 10.0f, -4.0f, 2.5f };
    const Quat rot = Quat::FromAxisAngle(Vec3{ 0.0f, 1.0f, 0.0f }, std::numbers::pi_v<float> * 0.5f);
    const Vec3 scl{ 2.0f, 3.0f, 4.0f };

    TransformComponent tf{ pos, rot, scl };
    const Mat4 trsMat = tf.ToMat4();
    const Mat4 expected = Mat4::Translate(pos) * rot.ToMat4() * Mat4::Scale(scl);
    CHECK(ApproxMat4(trsMat, expected, 1e-4f));

    // Fixed-step interpolation between prev and curr
    TransformComponent curr{
        Vec3{ 20.0f, 0.0f, 0.0f },
        Quat::FromAxisAngle(Vec3{ 0.0f, 1.0f, 0.0f }, std::numbers::pi_v<float> * 0.5f),
        Vec3{ 3.0f, 3.0f, 3.0f }
    };
    PreviousTransformComponent prev{
        Vec3{ 10.0f, 0.0f, 0.0f },
        Quat::Identity(),
        Vec3{ 1.0f, 1.0f, 1.0f }
    };

    const TransformComponent mid = prev.InterpolateTo(curr, 0.5f);
    CHECK(ApproxVec3(mid.position, Vec3{ 15.0f, 0.0f, 0.0f }));
    CHECK(ApproxVec3(mid.scale, Vec3{ 2.0f, 2.0f, 2.0f }));

    // Midpoint rotation of 90 deg around Y should be 45 deg around Y
    const Quat expectedMidRot = Quat::FromAxisAngle(Vec3{ 0.0f, 1.0f, 0.0f }, std::numbers::pi_v<float> * 0.25f);
    const Vec3 rotatedAxis = mid.rotation * Vec3{ 1.0f, 0.0f, 0.0f };
    const Vec3 expectedAxis = expectedMidRot * Vec3{ 1.0f, 0.0f, 0.0f };
    CHECK(ApproxVec3(rotatedAxis, expectedAxis, 1e-3f));
}

// ---------------------------------------------------------------------------
// 2b. Monotonic Epoch Dirty Tracking + Write Provenance (Zero Feedback Loops)
// ---------------------------------------------------------------------------
TEST_CASE("ECS Pass 2: Monotonic dirty epochs and write provenance prevent physics feedback loops")
{
    EcsRegistry reg;
    EcsCommandQueue q;

    Entity e = reg.CreateEntity();
    reg.AddComponent(e, TransformComponent::Identity(), q);
    reg.FlushCommands(q);

    auto& tfPool = reg.Pool<TransformComponent>();
    uint64_t physicsPreSyncEpoch = 0;
    uint64_t renderSyncEpoch     = 0;

    // Step 1: Initial command flush is visible to both PhysicsPreSync (Command|Gameplay) and RenderSync (Any)
    {
        const auto view = tfPool.ReadView();
        REQUIRE(view.Count() == 1);
        CHECK(view.IsDirtyFrom(0, physicsPreSyncEpoch, WriteSource::Command | WriteSource::Gameplay) == true);
        CHECK(view.IsDirtySince(0, renderSyncEpoch) == true);
    }
    physicsPreSyncEpoch = reg.CurrentEpoch();
    renderSyncEpoch     = reg.CurrentEpoch();

    // Step 2: 0-step frame — nothing writes; nothing is dirty
    {
        const auto view = tfPool.ReadView();
        CHECK(view.IsDirtyFrom(0, physicsPreSyncEpoch, WriteSource::Command | WriteSource::Gameplay) == false);
        CHECK(view.IsDirtySince(0, renderSyncEpoch) == false);
    }

    // Step 3: Gameplay teleports entity in Frame N, then Frame N has 0 physics substeps!
    const uint64_t gameplayEpoch = reg.AdvanceEpoch();
    {
        auto writeView = tfPool.WriteView(gameplayEpoch, WriteSource::Gameplay);
        TransformComponent moved = writeView.data[0];
        moved.position = Vec3{ 5.0f, 10.0f, 0.0f };
        writeView.Write(0, moved);
    }
    // RenderSync consumes Frame N's gameplay write, updating renderSyncEpoch
    {
        const auto view = tfPool.ReadView();
        CHECK(view.IsDirtySince(0, renderSyncEpoch) == true);
        renderSyncEpoch = reg.CurrentEpoch();
    }
    // Physics did NOT run on Frame N (0 substeps), so physicsPreSyncEpoch stayed behind.
    // On Frame N+1, PhysicsPreSync MUST still see the gameplay teleport (never dropped!).
    {
        const auto view = tfPool.ReadView();
        CHECK(view.IsDirtyFrom(0, physicsPreSyncEpoch, WriteSource::Command | WriteSource::Gameplay) == true);
        physicsPreSyncEpoch = reg.CurrentEpoch();
    }

    // Step 4: Multi-substep frame (Substep 1 & Substep 2) — Physics writes back with WriteSource::Physics
    for (int substep = 0; substep < 2; ++substep)
    {
        const uint64_t physEpoch = reg.AdvanceEpoch();
        auto writeView = tfPool.WriteView(physEpoch, WriteSource::Physics);
        writeView.data[0].position.y -= 1.0f;
        writeView.MarkDirty(0);

        // Next substep's PhysicsPreSync checks for Gameplay|Command writes:
        // Must be FALSE despite physEpoch > physicsPreSyncEpoch (prevents feedback loop!)
        const auto readView = tfPool.ReadView();
        CHECK(readView.epochs[0] > physicsPreSyncEpoch);
        CHECK(readView.IsDirtyFrom(0, physicsPreSyncEpoch, WriteSource::Command | WriteSource::Gameplay) == false);
    }

    // RenderSync runs once after both substeps and sees the final physics write ONCE (no duplicates)
    {
        const auto view = tfPool.ReadView();
        CHECK(view.IsDirtySince(0, renderSyncEpoch) == true);
        CHECK(ApproxEq(view.data[0].position.y, 8.0f));
        renderSyncEpoch = reg.CurrentEpoch();
        CHECK(view.IsDirtySince(0, renderSyncEpoch) == false);
    }
}

// ---------------------------------------------------------------------------
// 3. Scheduler — Phases, MainThread Affinity, Resource Conflict Masks,
//    and EcsCommandQueue& passed into execute()
// ---------------------------------------------------------------------------
TEST_CASE("ECS Pass 2: Scheduler phases, MainThread affinity, ResourceMask conflicts, and command enqueue")
{
    BlockingThreadPool& pool = BlockingThreadPool::Get();
    pool.Init(4);

    EcsRegistry reg;
    EcsCommandQueue q;

    const std::thread::id mainTid = std::this_thread::get_id();
    std::vector<int> phaseTrace;
    std::atomic<bool> mainRanOnMainThread{ false };
    std::atomic<int>  workerRuns{ 0 };
    std::vector<int>  physOrder;

    // Two FixedStep systems with EMPTY component masks, conflicting ONLY via ResourceBit::PhysicsWorld
    SystemDescriptor prePhysSys;
    prePhysSys.name     = "PrePhysSpawn";
    prePhysSys.phase    = SchedulePhase::PrePhysics;
    prePhysSys.affinity = ThreadAffinity::MainThread;
    prePhysSys.execute  = [&](EcsRegistry& r, EcsCommandQueue& cmdQ, float, uint64_t) {
        CHECK(std::this_thread::get_id() == mainTid);
        phaseTrace.push_back(0);
        // Enqueue structural command directly using the passed EcsCommandQueue&
        Entity e = r.CreateEntity();
        r.AddComponent(e, TransformComponent{ Vec3{ 1.0f, 2.0f, 3.0f }, Quat::Identity(), Vec3{ 1.0f } }, cmdQ);
    };

    SystemDescriptor physWrite1;
    physWrite1.name                  = "PhysPreSync";
    physWrite1.phase                 = SchedulePhase::FixedStep;
    physWrite1.affinity              = ThreadAffinity::AnyThread;
    physWrite1.access.resourceWrites = ResourceBit::PhysicsWorld;
    physWrite1.execute               = [&](EcsRegistry&, EcsCommandQueue&, float, uint64_t) {
        physOrder.push_back(1);
        workerRuns.fetch_add(1, std::memory_order_relaxed);
    };

    SystemDescriptor physWrite2;
    physWrite2.name                  = "PhysStep";
    physWrite2.phase                 = SchedulePhase::FixedStep;
    physWrite2.affinity              = ThreadAffinity::AnyThread;
    physWrite2.access.resourceReads  = ResourceBit::PhysicsWorld;
    physWrite2.access.resourceWrites = ResourceBit::PhysicsWorld;
    physWrite2.execute               = [&](EcsRegistry&, EcsCommandQueue&, float, uint64_t) {
        physOrder.push_back(2);
        workerRuns.fetch_add(1, std::memory_order_relaxed);
    };

    SystemDescriptor physParallelRead;
    physParallelRead.name                 = "PhysPostRead";
    physParallelRead.phase                = SchedulePhase::FixedStep;
    physParallelRead.affinity             = ThreadAffinity::AnyThread;
    physParallelRead.access.resourceReads = ResourceBit::PhysicsWorld;
    physParallelRead.execute              = [&](EcsRegistry&, EcsCommandQueue&, float, uint64_t) {
        physOrder.push_back(3);
        workerRuns.fetch_add(1, std::memory_order_relaxed);
    };

    SystemDescriptor postPhysSys;
    postPhysSys.name     = "PostPhys";
    postPhysSys.phase    = SchedulePhase::PostPhysics;
    postPhysSys.affinity = ThreadAffinity::AnyThread;
    postPhysSys.execute  = [&](EcsRegistry& r, EcsCommandQueue&, float, uint64_t) {
        // Entity spawned in PrePhysics must be visible after FlushCommands()
        CHECK(r.Pool<TransformComponent>().Count() == 1);
        phaseTrace.push_back(2);
    };

    SystemDescriptor renderSyncSys;
    renderSyncSys.name                  = "RenderMain";
    renderSyncSys.phase                 = SchedulePhase::RenderSync;
    renderSyncSys.affinity              = ThreadAffinity::MainThread;
    renderSyncSys.access.resourceWrites = ResourceBit::Scene;
    renderSyncSys.execute               = [&](EcsRegistry&, EcsCommandQueue&, float, uint64_t) {
        mainRanOnMainThread.store(std::this_thread::get_id() == mainTid, std::memory_order_relaxed);
        phaseTrace.push_back(3);
    };

    std::vector<SystemDescriptor> descs = {
        prePhysSys, physWrite1, physWrite2, physParallelRead, postPhysSys, renderSyncSys
    };
    const PhaseSchedule schedule = BuildPhaseSchedule(descs);

    // FixedStep resource conflicts must separate physWrite1 -> physWrite2 -> physParallelRead into 3 layers
    REQUIRE(schedule.LayersFor(SchedulePhase::PrePhysics).size()  == 1);
    REQUIRE(schedule.LayersFor(SchedulePhase::FixedStep).size()   == 3);
    REQUIRE(schedule.LayersFor(SchedulePhase::PostPhysics).size() == 1);
    REQUIRE(schedule.LayersFor(SchedulePhase::RenderSync).size()  == 1);

    // Tick the full phase pipeline with FlushCommands boundaries
    DispatchPhase(schedule, SchedulePhase::PrePhysics, reg, q, pool, 0.016f);
    reg.FlushCommands(q);

    DispatchPhase(schedule, SchedulePhase::FixedStep, reg, q, pool, 1.0f / 60.0f);
    reg.FlushCommands(q);

    DispatchPhase(schedule, SchedulePhase::PostPhysics, reg, q, pool, 0.016f);
    reg.FlushCommands(q);

    DispatchPhase(schedule, SchedulePhase::RenderSync, reg, q, pool, 0.016f);
    reg.FlushCommands(q);
    reg.ClearTombstones();

    CHECK(mainRanOnMainThread.load() == true);
    CHECK(workerRuns.load() == 3);
    REQUIRE(physOrder.size() == 3);
    CHECK(physOrder[0] == 1);
    CHECK(physOrder[1] == 2);
    CHECK(physOrder[2] == 3);
    REQUIRE(phaseTrace.size() == 3);
    CHECK(phaseTrace[0] == 0);
    CHECK(phaseTrace[1] == 2);
    CHECK(phaseTrace[2] == 3);

    pool.Shutdown();
}
