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
#include <Engine/Ecs/Components/MeshComponent.h>
#include <Engine/Ecs/Components/TransformComponent.h>
#include <Engine/Ecs/Components/RenderableHandleComponent.h>

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
    RenderSyncSystem sys(*scene, resolver);
    SystemDescriptor desc = sys.Descriptor();

    Entity e = reg.CreateEntity();
    reg.AddComponent(e, MeshComponent{ 1, 2 }, q);
    reg.AddComponent(e, TransformComponent{}, q);
    reg.FlushCommands(q);

    // The descriptor's execute is what the scheduler calls — it must be wired
    // to the real Execute(), not an empty lambda.
    desc.execute(reg, q, 0.016f, reg.AdvanceEpoch());
    reg.FlushCommands(q);

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
    sysA.execute = [](EcsRegistry&, EcsCommandQueue&, float, uint64_t) { s_runCount.fetch_add(1, std::memory_order_relaxed); };

    SystemDescriptor sysB;
    sysB.name = "B";
    sysB.access = {};
    sysB.execute = [](EcsRegistry&, EcsCommandQueue&, float, uint64_t) { s_runCount.fetch_add(1, std::memory_order_relaxed); };

    SystemDescriptor sysC;
    sysC.name = "C";
    sysC.access = {};
    sysC.execute = [](EcsRegistry&, EcsCommandQueue&, float, uint64_t) { s_runCount.fetch_add(1, std::memory_order_relaxed); };

    SystemDescriptor sysD;
    sysD.name = "D";
    sysD.access = {};
    sysD.execute = [](EcsRegistry&, EcsCommandQueue&, float, uint64_t) { s_runCount.fetch_add(1, std::memory_order_relaxed); };

    std::vector<SystemDescriptor> descs = { sysA, sysB, sysC, sysD };
    auto layers = BuildExecutionLayers(descs);
    REQUIRE(layers.size() == 1); // empty masks -> one parallel layer

    EcsRegistry reg;
    EcsCommandQueue q;
    for (int iter = 0; iter < 2000; ++iter)
    {
        s_runCount.store(0, std::memory_order_relaxed);
        DispatchFrame(layers, reg, q, pool, 0.016f);
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
    reg.FlushCommands(q);

    reg.DestroyEntity(e1, q);
    reg.FlushCommands(q);

    // e2 reuses e1's index with a bumped generation.
    Entity e2 = reg.CreateEntity();
    REQUIRE(Index(e2) == Index(e1));
    reg.AddComponent(e2, WeakCompA{ 2 }, q);
    reg.FlushCommands(q);

    // Directly enqueue a stale remove command for the dead e1 handle
    // (bypassing reg.RemoveComponent's enqueue-time IsAlive check, which is
    // exactly what happens when the command is enqueued before destruction).
    q.EnqueueRemove(e1, static_cast<uint8_t>(ComponentId<WeakCompA>()));
    reg.FlushCommands(q);

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
    reg.FlushCommands(q);

    Entity e2 = reg.CreateEntity();
    REQUIRE(Index(e2) == Index(e1));
    reg.AddComponent(e2, WeakCompA{ 7 }, q);
    reg.FlushCommands(q);

    q.EnqueueDestroy(e1); // stale handle
    reg.FlushCommands(q);

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

    reg.FlushCommands(q);
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
    reg.FlushCommands(q);

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
    reg.FlushCommands(q);

    q.EnqueueSet(e1, WeakCompA{ 99 }); // overwrite
    q.EnqueueSet(e2, WeakCompA{ 42 }); // insert
    reg.FlushCommands(q);

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
    reg.FlushCommands(q);

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
    reg.FlushCommands(q);

    CachedView<WeakCompA> view(reg);
    CHECK(view.Count() == 3);

    reg.RemoveComponent<WeakCompA>(e2, q);
    reg.FlushCommands(q);

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
    reg.FlushCommands(q);

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
    reg.FlushCommands(q);

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
        reg.FlushCommands(q);
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

    reg.FlushCommands(q);
    CHECK(reg.Pool<WeakCompA>().Count() == kThreads * kPerThread);
    for (uint32_t i = 0; i < kThreads * kPerThread; ++i)
    {
        const WeakCompA* c = reg.Pool<WeakCompA>().Get(Index(entities[i]));
        REQUIRE(c != nullptr);
        CHECK(c->v == static_cast<int>(i));
    }
}

// ---------------------------------------------------------------------------
// Tier 1 Fix — Entity::Invalid == 0 and Make() bit masking
// ---------------------------------------------------------------------------
TEST_CASE("ECS: Entity::Invalid is 0 and Make masks index and generation")
{
    CHECK(Zero::Invalid == 0);
    CHECK(Index(Zero::Invalid) == 0);
    CHECK(Generation(Zero::Invalid) == 0);

    EcsRegistry reg;
    CHECK(reg.IsAlive(Zero::Invalid) == false);

    // Overflowing bit widths in Make() must not bleed across fields
    const Entity masked = Make((1u << 21) | 5u, (1u << 13) | 7u);
    CHECK(Index(masked) == 5u);
    CHECK(Generation(masked) == 7u);
}

// ---------------------------------------------------------------------------
// Tier 1 Fix — Heap-fallback payload freed on dead entities & unflushed queue
// ---------------------------------------------------------------------------
TEST_CASE("ECS: Heap-fallback payload freed on dead entity and unflushed queue teardown")
{
    {
        EcsRegistry reg;
        EcsCommandQueue q(64); // forces heap fallback for HugeComp (256 B)

        Entity e = reg.CreateEntity();
        (void)reg.Pool<HugeComp>(); // ensure pool exists
        reg.DestroyEntity(e, q);
        reg.FlushCommands(q);
        REQUIRE(reg.IsAlive(e) == false);

        // Enqueue oversized command for now-dead entity; DispatchAdd must free heapPtr.
        HugeComp hc{};
        hc.data[0] = 0xAB;
        q.EnqueueSet(e, hc);
        reg.FlushCommands(q);
        CHECK(reg.Pool<HugeComp>().Count() == 0);
    }

    {
        // Unflushed EcsCommandQueue destruction must drain and free heap payloads.
        EcsRegistry reg;
        Entity e = reg.CreateEntity();
        EcsCommandQueue q(64);
        HugeComp hc{};
        hc.data[0] = 0xCD;
        q.EnqueueAdd(e, hc);
        // Intentionally drop q without calling FlushCommands()
    }
}

// ---------------------------------------------------------------------------
// Tier 1 Fix — FrameSlab capacity rounding & raw Data() + memcpy readback
// ---------------------------------------------------------------------------
TEST_CASE("ECS: FrameSlab rounds capacity to baseAlignment and Data supports memcpy")
{
    FrameSlab slab(100, 64);
    CHECK(slab.Capacity() == 128);

    BigComp src{};
    for (size_t i = 0; i < sizeof(src.data); ++i)
        src.data[i] = static_cast<uint8_t>(i ^ 0x5A);

    const uint32_t off = slab.Store(src);
    REQUIRE(off != UINT32_MAX);
    const uint8_t* ptr = slab.Data(off, sizeof(BigComp));
    REQUIRE(ptr != nullptr);

    BigComp dst{};
    std::memcpy(&dst, ptr, sizeof(BigComp));
    for (size_t i = 0; i < sizeof(src.data); ++i)
        CHECK(dst.data[i] == src.data[i]);
}

// ---------------------------------------------------------------------------
// Tier 1 Fix — Scheduler DAG longest-path ordering prevents S2 jumping ahead of S1
// ---------------------------------------------------------------------------
TEST_CASE("ECS: Scheduler DAG ordering forbids later reader jumping ahead of skipped writer")
{
    // S0 writes A
    // S1 reads A, writes B (conflicts with S0 -> Layer 1)
    // S2 reads B (disjoint from S0, but depends on S1 -> must be Layer 2, NOT Layer 0!)
    std::vector<int> trace;

    SystemDescriptor s0{
        "S0",
        { ComponentMask{}, MaskOf<WeakCompA>() },
        [&](EcsRegistry&, EcsCommandQueue&, float, uint64_t) { trace.push_back(0); }
    };
    SystemDescriptor s1{
        "S1",
        { MaskOf<WeakCompA>(), MaskOf<WeakCompB>() },
        [&](EcsRegistry&, EcsCommandQueue&, float, uint64_t) { trace.push_back(1); }
    };
    SystemDescriptor s2{
        "S2",
        { MaskOf<WeakCompB>(), ComponentMask{} },
        [&](EcsRegistry&, EcsCommandQueue&, float, uint64_t) { trace.push_back(2); }
    };

    std::vector<SystemDescriptor> descs = { s0, s1, s2 };
    auto layers = BuildExecutionLayers(descs);
    REQUIRE(layers.size() == 3);
    CHECK(layers[0].systems.size() == 1);
    CHECK(layers[1].systems.size() == 1);
    CHECK(layers[2].systems.size() == 1);
    CHECK(layers[0].systems[0] == &descs[0]);
    CHECK(layers[1].systems[0] == &descs[1]);
    CHECK(layers[2].systems[0] == &descs[2]);

    BlockingThreadPool& pool = BlockingThreadPool::Get();
    pool.Init(4);
    EcsRegistry reg;
    EcsCommandQueue q;
    DispatchFrame(layers, reg, q, pool, 0.016f);
    REQUIRE(trace.size() == 3);
    CHECK(trace[0] == 0);
    CHECK(trace[1] == 1);
    CHECK(trace[2] == 2);
}

// ---------------------------------------------------------------------------
// Tier 1 Fix — CachedView outliving EcsRegistry detaches safely (no UAF) &
// concurrent Each()/Count() reads are race-free
// ---------------------------------------------------------------------------
TEST_CASE("ECS: CachedView detaches safely if EcsRegistry is destroyed first")
{
    auto reg = std::make_unique<EcsRegistry>();
    EcsCommandQueue q;
    Entity e = reg->CreateEntity();
    reg->AddComponent(e, WeakCompA{ 42 }, q);
    reg->FlushCommands(q);

    CachedView<WeakCompA> view(*reg);
    CHECK(view.Count() == 1);

    // Destroy registry BEFORE view goes out of scope
    reg.reset();
}

TEST_CASE("ECS: CachedView rebuilt during FlushCommands is safe for concurrent reads")
{
    EcsRegistry reg;
    EcsCommandQueue q;

    for (int i = 0; i < 64; ++i)
    {
        Entity e = reg.CreateEntity();
        reg.AddComponent(e, WeakCompA{ i }, q);
    }
    reg.FlushCommands(q);

    CachedView<WeakCompA> view(reg);
    CHECK(view.Count() == 64);

    // Add more and flush — view must already be rebuilt before Each/Count
    for (int i = 0; i < 36; ++i)
    {
        Entity e = reg.CreateEntity();
        reg.AddComponent(e, WeakCompA{ 64 + i }, q);
    }
    reg.FlushCommands(q);

    std::atomic<uint32_t> totalSeen{ 0 };
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t)
    {
        readers.emplace_back([&]() {
            CHECK(view.Count() == 100);
            uint32_t local = 0;
            view.Each([&](uint32_t) { ++local; });
            totalSeen.fetch_add(local, std::memory_order_relaxed);
        });
    }
    for (auto& th : readers) th.join();
    CHECK(totalSeen.load() == 400);
}

// ---------------------------------------------------------------------------
// Tier 1 Fix — BlockingThreadPool sealed pool enqueue_bulk does not deadlock
// ---------------------------------------------------------------------------
TEST_CASE("ECS: DispatchFrame on sealed BlockingThreadPool does not deadlock")
{
    BlockingThreadPool& pool = BlockingThreadPool::Get();
    pool.Init(4);

    std::atomic<int> runs{ 0 };
    SystemDescriptor s{
        "SealedTest",
        {},
        [&](EcsRegistry&, EcsCommandQueue&, float, uint64_t) { runs.fetch_add(1, std::memory_order_relaxed); }
    };
    std::vector<SystemDescriptor> descs = { s };
    auto layers = BuildExecutionLayers(descs);

    pool.StopAcceptingJobs();
    EcsRegistry reg;
    EcsCommandQueue q;
    DispatchFrame(layers, reg, q, pool, 0.016f);
    CHECK(runs.load() == 0);

    // Cleanly shut down the singleton pool so worker threads do not access
    // m_queue during CRT atexit teardown.
    pool.Shutdown();
}

// ---------------------------------------------------------------------------
// Tier 3 Optimization — ComponentView with non-first primary pool & EachExcluding
// + DispatchDestroy bit-scan across lo & hi mask words
// ---------------------------------------------------------------------------
TEST_CASE("ECS: ComponentView non-first primary pool and DispatchDestroy bit-scan")
{
    EcsRegistry reg;
    EcsCommandQueue q;

    // 10 entities with WeakCompA (id 110, hi), 2 also with WeakCompB (id 111, hi),
    // 1 of those also with WeakCompC (id 112, hi) and TransformComponent (id 0, lo).
    std::vector<Entity> ents;
    for (int i = 0; i < 10; ++i)
    {
        Entity e = reg.CreateEntity();
        reg.AddComponent(e, WeakCompA{ i }, q);
        if (i < 2)
            reg.AddComponent(e, WeakCompB{ static_cast<float>(i * 10) }, q);
        if (i == 0)
        {
            reg.AddComponent(e, WeakCompC{ 99.0 }, q);
            reg.AddComponent(e, TransformComponent{}, q);
        }
        ents.push_back(e);
    }
    reg.FlushCommands(q);

    // WeakCompB is second in template pack but smaller (2 < 10) -> m_primary = WeakCompB
    ComponentView<WeakCompA, WeakCompB> view(reg);
    CHECK(view.Count() == 2);

    int sumA = 0;
    float sumB = 0.0f;
    view.Each([&](Entity, const WeakCompA& a, const WeakCompB& b) {
        sumA += a.v;
        sumB += b.v;
    });
    CHECK(sumA == 1);      // 0 + 1
    CHECK(sumB == 10.0f);  // 0 + 10

    int exclCount = 0;
    view.EachExcluding<WeakCompC>([&](Entity, const WeakCompA& a, const WeakCompB& b) {
        CHECK(a.v == 1);
        CHECK(b.v == 10.0f);
        ++exclCount;
    });
    CHECK(exclCount == 1);

    // Destroy entity 0 (has lo bit 0 and hi bits 110, 111, 112)
    reg.DestroyEntity(ents[0], q);
    reg.FlushCommands(q);

    CHECK(reg.Pool<TransformComponent>().Count() == 0);
    CHECK(reg.Pool<WeakCompC>().Count() == 0);
    CHECK(reg.Pool<WeakCompA>().Count() == 9);
    CHECK(reg.Pool<WeakCompB>().Count() == 1);
}
