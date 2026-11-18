#include <doctest.h>
#include <Engine/Ecs/EcsRegistry.h>
#include <Engine/Ecs/Entity.h>
#include <Engine/Ecs/ComponentTrait.h>
#include <Engine/Ecs/ComponentMask.h>
#include <Engine/Ecs/EcsMask.h>
#include <Engine/Ecs/EcsCommandQueue.h>
#include <Engine/Ecs/ComponentPool.h>
#include <Engine/Ecs/CachedView.h>
#include <Engine/Ecs/ComponentView.h>
#include <Engine/Ecs/Scheduler.h>
#include <Engine/Ecs/FrameSlab.h>
#include <Engine/Ecs/Systems/RenderSyncSystem.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/Scene.h>

#include "JobSystem/BlockingThreadPool.h"
#include "ECS/Components/MeshComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/RenderableHandleComponent.h"

#include <atomic>
#include <thread>
#include <vector>
#include <cstring>
#include <cstdint>

using namespace Zero;

// ---------------------------------------------------------------------------
// Test component types — distinct IDs, no overlap with engine (0-8) or the
// existing TestEcs.cpp dummy components (100-102).
// ---------------------------------------------------------------------------
struct WeakCompA { int v; };
struct WeakCompB { float v; };
struct WeakCompC { double v; };
struct WeakCompD { int v; };            // deliberately never instantiated
struct BigComp   { uint8_t data[80]; }; // > 44 bytes -> FrameSlab path
struct HugeComp  { uint8_t data[256]; }; // oversized -> heap fallback path
struct NewComp   { int v; };            // concurrent first-use target

static_assert(std::is_trivially_copyable_v<WeakCompA>);
static_assert(std::is_trivially_copyable_v<WeakCompB>);
static_assert(std::is_trivially_copyable_v<WeakCompC>);
static_assert(std::is_trivially_copyable_v<WeakCompD>);
static_assert(std::is_trivially_copyable_v<BigComp>);
static_assert(std::is_trivially_copyable_v<HugeComp>);
static_assert(std::is_trivially_copyable_v<NewComp>);

REGISTER_COMPONENT(WeakCompA, 110)
REGISTER_COMPONENT(WeakCompB, 111)
REGISTER_COMPONENT(WeakCompC, 112)
REGISTER_COMPONENT(WeakCompD, 113)
REGISTER_COMPONENT(BigComp, 114)
REGISTER_COMPONENT(HugeComp, 115)
REGISTER_COMPONENT(NewComp, 116)

// ---------------------------------------------------------------------------
// Critical #1 — RenderSyncSystem::Descriptor().execute must actually run the
// system, not be a no-op lambda. If the system runs, entities with
// MeshComponent + TransformComponent gain a RenderableHandleComponent after
// the descriptor's execute is invoked.
// ---------------------------------------------------------------------------
TEST_CASE("ECS: RenderSyncSystem descriptor execute runs the system")
{
    SceneManager::Get().Init();
    Scene* scene = SceneManager::Get().GetMainScene();
    REQUIRE(scene != nullptr);

    MeshResolverFn resolver = [](uint32_t, uint32_t, MeshResolvedDesc& out) {
        out.vertexBuffer = {};
        out.indexBuffer  = {};
        out.pipeline     = {};
        out.indexCount   = 3;
        out.firstIndex   = 0;
        out.vertexOffset = 0;
        out.pipelineId   = 1;
        out.materialId   = 1;
        out.castsShadow  = true;
        out.isTransparent = false;
        return true;
    };

    EcsRegistry reg;
    EcsCommandQueue q;
    RenderSyncSystem sys(*scene, q, resolver);
    SystemDescriptor desc = sys.Descriptor();

    Entity e = reg.CreateEntity();
    reg.AddComponent(e, MeshComponent{ 1, 2 }, q);
    reg.AddComponent(e, TransformComponent{}, q);
    reg.FlushCommands(q, 1);

    // The descriptor's execute is what the scheduler calls — it must be wired
    // to the real Execute(), not an empty lambda.
    desc.execute(reg, 0.016f, 2);
    reg.FlushCommands(q, 3);

    CHECK(reg.Mask(Index(e)).Has(ComponentId<RenderableHandleComponent>()) == true);
}

// ---------------------------------------------------------------------------
// Critical #2 — Scheduler layer barrier. DispatchFrame must not return before
// every job in the layer has completed. With the counter race, Wait() can
// return early while jobs are still queued, so the run counter lags.
// ---------------------------------------------------------------------------
TEST_CASE("ECS: Scheduler DispatchFrame runs all systems in a layer before returning")
{
    BlockingThreadPool& pool = BlockingThreadPool::Get();
    pool.Init(4);

    static std::atomic<int> s_runCount{ 0 };

    SystemDescriptor sysA;
    sysA.name = "A";
    sysA.access = {};
    sysA.execute = [](EcsRegistry&, float, uint32_t) { s_runCount.fetch_add(1, std::memory_order_relaxed); };

    SystemDescriptor sysB;
    sysB.name = "B";
    sysB.access = {};
    sysB.execute = [](EcsRegistry&, float, uint32_t) { s_runCount.fetch_add(1, std::memory_order_relaxed); };

    SystemDescriptor sysC;
    sysC.name = "C";
    sysC.access = {};
    sysC.execute = [](EcsRegistry&, float, uint32_t) { s_runCount.fetch_add(1, std::memory_order_relaxed); };

    SystemDescriptor sysD;
    sysD.name = "D";
    sysD.access = {};
    sysD.execute = [](EcsRegistry&, float, uint32_t) { s_runCount.fetch_add(1, std::memory_order_relaxed); };

    std::vector<SystemDescriptor> descs = { sysA, sysB, sysC, sysD };
    auto layers = BuildExecutionLayers(descs);
    REQUIRE(layers.size() == 1); // empty masks -> one parallel layer

    EcsRegistry reg;
    for (int iter = 0; iter < 2000; ++iter)
    {
        s_runCount.store(0, std::memory_order_relaxed);
        DispatchFrame(layers, reg, pool, 0.016f, static_cast<uint32_t>(iter));
        CHECK(s_runCount.load(std::memory_order_relaxed) == 4);
    }
}

// ---------------------------------------------------------------------------
// High #4 — stale-handle RemoveComponent must be generation-checked. A command
// carrying a dead entity must not remove components from the recycled slot.
// ---------------------------------------------------------------------------
TEST_CASE("ECS: RemoveComponent with a stale handle is a no-op")
{
    EcsRegistry reg;
    EcsCommandQueue q;

    Entity e1 = reg.CreateEntity();
    reg.AddComponent(e1, WeakCompA{ 1 }, q);
    reg.FlushCommands(q, 1);

    reg.DestroyEntity(e1, q);
    reg.FlushCommands(q, 2);

    // e2 reuses e1's index with a bumped generation.
    Entity e2 = reg.CreateEntity();
    REQUIRE(Index(e2) == Index(e1));
    reg.AddComponent(e2, WeakCompA{ 2 }, q);
    reg.FlushCommands(q, 3);

    // Directly enqueue a stale remove command for the dead e1 handle
    // (bypassing reg.RemoveComponent's enqueue-time IsAlive check, which is
    // exactly what happens when the command is enqueued before destruction).
    q.EnqueueRemove(e1, static_cast<uint8_t>(ComponentId<WeakCompA>()));
    reg.FlushCommands(q, 4);

    // e2 must keep its component.
    const WeakCompA* c = reg.Pool<WeakCompA>().Get(Index(e2));
    REQUIRE(c != nullptr);
    CHECK(c->v == 2);
}

// ---------------------------------------------------------------------------
// High #4 — stale-handle DestroyEntity must be generation-checked.
// ---------------------------------------------------------------------------
TEST_CASE("ECS: DestroyEntity with a stale handle is a no-op")
{
    EcsRegistry reg;
    EcsCommandQueue q;

    Entity e1 = reg.CreateEntity();
    reg.DestroyEntity(e1, q);
    reg.FlushCommands(q, 1);

    Entity e2 = reg.CreateEntity();
    REQUIRE(Index(e2) == Index(e1));
    reg.AddComponent(e2, WeakCompA{ 7 }, q);
    reg.FlushCommands(q, 2);

    q.EnqueueDestroy(e1); // stale handle
    reg.FlushCommands(q, 3);

    CHECK(reg.IsAlive(e2) == true);
    CHECK(reg.Pool<WeakCompA>().Get(Index(e2)) != nullptr);
}

// ---------------------------------------------------------------------------
// High #5 — views over a registered-but-never-instantiated component type must
// not crash; they must iterate zero entities.
// ---------------------------------------------------------------------------
TEST_CASE("ECS: views over a never-instantiated component type don't crash")
{
    EcsRegistry reg;
    // WeakCompD is registered but never added to any entity.

    {
        CachedView<WeakCompD> view(reg);
        uint32_t count = 0;
        view.Each([&](uint32_t) { ++count; });
        CHECK(count == 0);
        CHECK(view.Count() == 0);
    }
    {
        ComponentView<WeakCompD> view(reg);
        uint32_t count = 0;
        view.Each([&](Entity, const WeakCompD&) { ++count; });
        CHECK(count == 0);
    }
}

// ---------------------------------------------------------------------------
// High #6 — concurrent first-use of a new component type. Pool creation must be
// safe under concurrent AddComponent. (Run under TSan for the race detector.)
// ---------------------------------------------------------------------------
TEST_CASE("ECS: concurrent first-use of a new component type is safe")
{
    EcsRegistry reg;
    EcsCommandQueue q;
    constexpr uint32_t kThreads = 8;

    std::vector<Entity> entities(kThreads);
    for (auto& e : entities) e = reg.CreateEntity();

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (uint32_t t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&, t]() {
            reg.AddComponent(entities[t], NewComp{ static_cast<int>(t) }, q);
        });
    }
    for (auto& th : threads) th.join();

    reg.FlushCommands(q, 1);
    CHECK(reg.Pool<NewComp>().Count() == kThreads);
    for (uint32_t t = 0; t < kThreads; ++t)
        CHECK(reg.Pool<NewComp>().Get(Index(entities[t]))->v == static_cast<int>(t));
}

// ---------------------------------------------------------------------------
// High #7 — duplicate AddComponent must be idempotent (Set semantics, last
// write wins) instead of asserting on an already-present component.
// ---------------------------------------------------------------------------
TEST_CASE("ECS: duplicate AddComponent is idempotent (last write wins)")
{
    EcsRegistry reg;
    EcsCommandQueue q;
    Entity e = reg.CreateEntity();

    reg.AddComponent(e, WeakCompA{ 1 }, q);
    reg.AddComponent(e, WeakCompA{ 2 }, q); // duplicate
    reg.FlushCommands(q, 1);

    CHECK(reg.Pool<WeakCompA>().Count() == 1);
    REQUIRE(reg.Pool<WeakCompA>().Get(Index(e)) != nullptr);
    CHECK(reg.Pool<WeakCompA>().Get(Index(e))->v == 2);
}

// ---------------------------------------------------------------------------
// High #7 — explicit SetComponent: overwrites existing, inserts missing.
// ---------------------------------------------------------------------------
TEST_CASE("ECS: SetComponent overwrites existing and inserts missing")
{
    EcsRegistry reg;
    EcsCommandQueue q;
    Entity e1 = reg.CreateEntity();
    Entity e2 = reg.CreateEntity();

    reg.AddComponent(e1, WeakCompA{ 1 }, q);
    reg.FlushCommands(q, 1);

    q.EnqueueSet(e1, WeakCompA{ 99 }); // overwrite
    q.EnqueueSet(e2, WeakCompA{ 42 }); // insert
    reg.FlushCommands(q, 2);

    CHECK(reg.Pool<WeakCompA>().Count() == 2);
    REQUIRE(reg.Pool<WeakCompA>().Get(Index(e1)) != nullptr);
    CHECK(reg.Pool<WeakCompA>().Get(Index(e1))->v == 99);
    REQUIRE(reg.Pool<WeakCompA>().Get(Index(e2)) != nullptr);
    CHECK(reg.Pool<WeakCompA>().Get(Index(e2))->v == 42);
}

// ---------------------------------------------------------------------------
// Medium #8 — ComponentView::Count() must return the exact number of entities
// matching the full mask, not the size of the smallest pool.
// ---------------------------------------------------------------------------
TEST_CASE("ECS: ComponentView::Count matches exact match count")
{
    EcsRegistry reg;
    EcsCommandQueue q;

    // 5 entities with only A
    for (int i = 0; i < 5; ++i)
    {
        Entity e = reg.CreateEntity();
        reg.AddComponent(e, WeakCompA{ i }, q);
    }
    // 3 entities with A+B
    for (int i = 0; i < 3; ++i)
    {
        Entity e = reg.CreateEntity();
        reg.AddComponent(e, WeakCompA{ i }, q);
        reg.AddComponent(e, WeakCompB{ float(i) }, q);
    }
    // 2 entities with only B
    for (int i = 0; i < 2; ++i)
    {
        Entity e = reg.CreateEntity();
        reg.AddComponent(e, WeakCompB{ float(i) }, q);
    }
    reg.FlushCommands(q, 1);

    ComponentView<WeakCompA, WeakCompB> view(reg);
    CHECK(view.Count() == 3); // exact matches only

    uint32_t iterated = 0;
    view.Each([&](Entity, const WeakCompA&, const WeakCompB&) { ++iterated; });
    CHECK(iterated == 3);
}

// ---------------------------------------------------------------------------
// Medium — CachedView must rebuild correctly after removals (swap-and-pop
// moves the last dense entry; the cached list must reflect it).
// ---------------------------------------------------------------------------
TEST_CASE("ECS: CachedView rebuilds correctly after removal (swap-and-pop)")
{
    EcsRegistry reg;
    EcsCommandQueue q;

    Entity e1 = reg.CreateEntity();
    Entity e2 = reg.CreateEntity();
    Entity e3 = reg.CreateEntity();
    reg.AddComponent(e1, WeakCompA{ 1 }, q);
    reg.AddComponent(e2, WeakCompA{ 2 }, q);
    reg.AddComponent(e3, WeakCompA{ 3 }, q);
    reg.FlushCommands(q, 1);

    CachedView<WeakCompA> view(reg);
    CHECK(view.Count() == 3);

    reg.RemoveComponent<WeakCompA>(e2, q);
    reg.FlushCommands(q, 2);

    CHECK(view.Count() == 2);
    uint32_t seen = 0;
    view.Each([&](uint32_t) { ++seen; });
    CHECK(seen == 2);

    CHECK(reg.Pool<WeakCompA>().Count() == 2);
    REQUIRE(reg.Pool<WeakCompA>().Get(Index(e1)) != nullptr);
    CHECK(reg.Pool<WeakCompA>().Get(Index(e1))->v == 1);
    REQUIRE(reg.Pool<WeakCompA>().Get(Index(e3)) != nullptr);
    CHECK(reg.Pool<WeakCompA>().Get(Index(e3))->v == 3);
    CHECK(reg.Pool<WeakCompA>().Get(Index(e2)) == nullptr);
}

// ---------------------------------------------------------------------------
// Medium #9 — FrameSlab must report overflow instead of asserting / writing
// out of bounds.
// ---------------------------------------------------------------------------
TEST_CASE("ECS: FrameSlab reports overflow instead of corrupting")
{
    FrameSlab slab(64, 64);
    uint8_t data[32] = {};
    uint32_t off1 = slab.Allocate(data, 32, 32);
    CHECK(off1 == 0);
    uint32_t off2 = slab.Allocate(data, 32, 32);
    CHECK(off2 == 32);
    uint32_t off3 = slab.Allocate(data, 32, 32); // would exceed capacity
    CHECK(off3 == UINT32_MAX);
}

// ---------------------------------------------------------------------------
// Medium #9 — EcsCommandQueue must heap-fall-back when its slab is exhausted
// instead of asserting or corrupting memory.
// ---------------------------------------------------------------------------
TEST_CASE("ECS: command queue heap-falls back when slab is exhausted")
{
    EcsCommandQueue q(128); // tiny slab
    EcsRegistry reg;
    Entity e = reg.CreateEntity();

    HugeComp hc{};
    for (size_t i = 0; i < sizeof(hc.data); ++i) hc.data[i] = static_cast<uint8_t>(i);
    reg.AddComponent(e, hc, q);
    reg.FlushCommands(q, 1);

    const HugeComp* got = reg.Pool<HugeComp>().Get(Index(e));
    REQUIRE(got != nullptr);
    for (size_t i = 0; i < sizeof(hc.data); ++i)
        CHECK(got->data[i] == static_cast<uint8_t>(i));
}

// ---------------------------------------------------------------------------
// Coverage — oversized component (> 44 bytes) round-trips through the slab.
// ---------------------------------------------------------------------------
TEST_CASE("ECS: oversized component round-trips through the FrameSlab")
{
    EcsRegistry reg;
    EcsCommandQueue q;
    Entity e = reg.CreateEntity();

    BigComp b{};
    for (size_t i = 0; i < sizeof(b.data); ++i) b.data[i] = static_cast<uint8_t>(i * 2 + 1);
    reg.AddComponent(e, b, q);
    reg.FlushCommands(q, 1);

    const BigComp* got = reg.Pool<BigComp>().Get(Index(e));
    REQUIRE(got != nullptr);
    for (size_t i = 0; i < sizeof(b.data); ++i)
        CHECK(got->data[i] == static_cast<uint8_t>(i * 2 + 1));
}

// ---------------------------------------------------------------------------
// Medium #12 — generation must never wrap in a way that makes a fresh entity
// unaddressable or a stale handle look alive. Slots are retired at max gen.
// ---------------------------------------------------------------------------
TEST_CASE("ECS: generation wrap retires slots — fresh entities stay alive")
{
    EcsRegistry reg;
    EcsCommandQueue q;

    // Churn one slot far beyond the 12-bit generation field.
    for (uint32_t i = 0; i < 5000; ++i)
    {
        Entity e = reg.CreateEntity();
        reg.DestroyEntity(e, q);
        reg.FlushCommands(q, i + 1);
    }

    Entity fresh = reg.CreateEntity();
    CHECK(reg.IsAlive(fresh) == true);
    CHECK(Index(fresh) < (1u << 20));
}

// ---------------------------------------------------------------------------
// Coverage — many threads enqueue commands; a single main-thread flush must
// apply every command exactly once.
// ---------------------------------------------------------------------------
TEST_CASE("ECS: concurrent enqueue from many threads applied by single flush")
{
    EcsRegistry reg;
    EcsCommandQueue q;
    constexpr uint32_t kThreads = 8;
    constexpr uint32_t kPerThread = 500;

    std::vector<Entity> entities(kThreads * kPerThread);
    for (auto& e : entities) e = reg.CreateEntity();

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (uint32_t t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&, t]() {
            for (uint32_t i = 0; i < kPerThread; ++i)
            {
                const uint32_t idx = t * kPerThread + i;
                reg.AddComponent(entities[idx], WeakCompA{ static_cast<int>(idx) }, q);
            }
        });
    }
    for (auto& th : threads) th.join();

    reg.FlushCommands(q, 1);
    CHECK(reg.Pool<WeakCompA>().Count() == kThreads * kPerThread);
    for (uint32_t i = 0; i < kThreads * kPerThread; ++i)
    {
        const WeakCompA* c = reg.Pool<WeakCompA>().Get(Index(entities[i]));
        REQUIRE(c != nullptr);
        CHECK(c->v == static_cast<int>(i));
    }
}
