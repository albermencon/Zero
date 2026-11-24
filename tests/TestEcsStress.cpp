#include <doctest.h>
#include <Engine/Ecs/CachedView.h>
#include <Engine/Ecs/ComponentMask.h>
#include <Engine/Ecs/ComponentPool.h>
#include <Engine/Ecs/ComponentTrait.h>
#include <Engine/Ecs/ComponentView.h>
#include <Engine/Ecs/EcsCommandQueue.h>
#include <Engine/Ecs/EcsMask.h>
#include <Engine/Ecs/EcsRegistry.h>
#include <Engine/Ecs/Entity.h>
#include <Engine/Ecs/Scheduler.h>
#include "JobSystem/BlockingThreadPool.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <thread>
#include <vector>

using namespace Zero;

namespace
{
    struct StressPos
    {
        float x, y, z;
        uint32_t id;
    };

    struct StressVel
    {
        float vx, vy, vz;
    };

    struct StressTag
    {
        uint32_t flags;
    };

    struct alignas(32) StressAlignedBig
    {
        uint32_t tag;
        uint8_t payload[124];
    };

    struct StressCounter
    {
        uint64_t ticks;
    };

    static_assert(std::is_trivially_copyable_v<StressPos>);
    static_assert(std::is_trivially_copyable_v<StressVel>);
    static_assert(std::is_trivially_copyable_v<StressTag>);
    static_assert(std::is_trivially_copyable_v<StressAlignedBig>);
    static_assert(std::is_trivially_copyable_v<StressCounter>);
}

REGISTER_COMPONENT(StressPos, 123)
REGISTER_COMPONENT(StressVel, 124)
REGISTER_COMPONENT(StressTag, 125)
REGISTER_COMPONENT(StressAlignedBig, 126)
REGISTER_COMPONENT(StressCounter, 127)

namespace
{
    template<typename T>
    void VerifyPoolBijection(const ComponentPool<T>& pool)
    {
        const uint32_t n = pool.Count();
        REQUIRE(pool.data.size() == n);
        REQUIRE(pool.dense_to_entity.size() == n);
        REQUIRE(pool.modifiedEpoch.size() == n);
        REQUIRE(pool.modifiedSource.size() == n);

        for (uint32_t i = 0; i < n; ++i)
        {
            const uint32_t eidx = pool.dense_to_entity[i];
            REQUIRE(eidx < pool.sparse.size());
            REQUIRE(pool.sparse[eidx] == i);
        }
    }
}

TEST_CASE("ECS Stress: High-volume entity & component churn with CachedView & bijection invariants")
{
    auto reg = std::make_unique<EcsRegistry>();
    EcsCommandQueue q;

    constexpr uint32_t kInitialEntities = 50'000;
    constexpr uint32_t kChurnFrames     = 20;

    std::vector<Entity> live;
    live.reserve(kInitialEntities * 2);

    std::mt19937 rng(0x1337BEEFu);

    for (uint32_t i = 0; i < kInitialEntities; ++i)
    {
        Entity e = reg->CreateEntity();
        live.push_back(e);
        reg->AddComponent(e, StressPos{ static_cast<float>(i), 1.0f, 2.0f, i }, q);
        if ((i % 2) == 0)
            reg->AddComponent(e, StressVel{ 0.5f, 0.25f, 0.125f }, q);
        if ((i % 3) == 0)
            reg->AddComponent(e, StressTag{ i }, q);
    }
    reg->FlushCommands(q);

    CachedView<StressPos, StressVel> posVelView(*reg);
    CachedView<StressPos, StressTag> posTagView(*reg);

    for (uint32_t frame = 0; frame < kChurnFrames; ++frame)
    {
        // Shuffle live list and destroy ~20% of entities
        std::shuffle(live.begin(), live.end(), rng);
        const uint32_t destroyCount = static_cast<uint32_t>(live.size() / 5);
        for (uint32_t i = 0; i < destroyCount; ++i)
        {
            reg->DestroyEntity(live.back(), q);
            live.pop_back();
        }

        // Mutate components on remaining entities
        const uint32_t liveCount = static_cast<uint32_t>(live.size());
        for (uint32_t i = 0; i < liveCount; ++i)
        {
            const Entity e = live[i];
            const uint32_t idx = Index(e);
            if ((i & 7u) == 0u)
            {
                reg->SetComponent(e, StressPos{ static_cast<float>(frame), 2.0f, 3.0f, idx }, q);
            }
            else if ((i & 7u) == 1u)
            {
                if (reg->Mask(idx).Has(ComponentId<StressVel>()))
                    reg->RemoveComponent<StressVel>(e, q);
                else
                    reg->AddComponent(e, StressVel{ 1.0f, 2.0f, 3.0f }, q);
            }
            else if ((i & 7u) == 2u)
            {
                if (reg->Mask(idx).Has(ComponentId<StressTag>()))
                    reg->RemoveComponent<StressTag>(e, q);
                else
                    reg->AddComponent(e, StressTag{ frame }, q);
            }
        }

        // Replenish destroyed entities
        for (uint32_t i = 0; i < destroyCount; ++i)
        {
            Entity e = reg->CreateEntity();
            live.push_back(e);
            reg->AddComponent(e, StressPos{ static_cast<float>(frame), 0.0f, 0.0f, Index(e) }, q);
            if ((i & 1u) == 0u)
                reg->AddComponent(e, StressVel{ 0.1f, 0.2f, 0.3f }, q);
        }

        reg->FlushCommands(q);

        // Verify structural bijections
        VerifyPoolBijection(reg->Pool<StressPos>());
        VerifyPoolBijection(reg->Pool<StressVel>());
        VerifyPoolBijection(reg->Pool<StressTag>());

        // Verify CachedView matches ComponentView exactly
        ComponentView<StressPos, StressVel> directPosVel(*reg);
        ComponentView<StressPos, StressTag> directPosTag(*reg);
        CHECK(posVelView.Count() == directPosVel.Count());
        CHECK(posTagView.Count() == directPosTag.Count());
        CHECK(reg->DestroyedEntities().size() == destroyCount);

        reg->ClearTombstones();
        CHECK(reg->DestroyedEntities().empty());
    }

    CHECK(reg->Pool<StressPos>().Count() == live.size());
}

TEST_CASE("ECS Stress: Multi-threaded command storm & concurrent entity minting")
{
    auto reg = std::make_unique<EcsRegistry>();
    // Intentionally small slab to force concurrent slab + heap fallback allocations.
    EcsCommandQueue q(8192);

    // Pre-warm pools before concurrent AddComponent waves if needed, or let
    // GetOrCreatePool mutex handle concurrent first-use.
    constexpr uint32_t kThreads         = 8;
    constexpr uint32_t kWaves           = 8;
    constexpr uint32_t kEntitiesPerWave = 1500;

    std::vector<std::vector<Entity>> threadEntities(kThreads);
    std::atomic<uint64_t> totalCreated{ 0 };

    for (uint32_t wave = 0; wave < kWaves; ++wave)
    {
        std::vector<std::thread> workers;
        workers.reserve(kThreads);

        for (uint32_t t = 0; t < kThreads; ++t)
        {
            workers.emplace_back([&, t, wave]() {
                auto& local = threadEntities[t];

                // Destroy half of previous wave's entities from this thread
                const size_t toDestroy = local.size() / 2;
                for (size_t i = 0; i < toDestroy; ++i)
                {
                    reg->DestroyEntity(local.back(), q);
                    local.pop_back();
                }

                // Upsert remaining entities from previous wave
                for (size_t i = 0; i < local.size(); ++i)
                {
                    reg->SetComponent(local[i], StressPos{
                        static_cast<float>(wave), static_cast<float>(t), 0.0f, Index(local[i])
                    }, q);
                }

                // Mint new entities and attach inline + oversized components
                for (uint32_t i = 0; i < kEntitiesPerWave; ++i)
                {
                    Entity e = reg->CreateEntity();
                    local.push_back(e);
                    totalCreated.fetch_add(1, std::memory_order_relaxed);

                    reg->AddComponent(e, StressPos{
                        static_cast<float>(i), static_cast<float>(t), static_cast<float>(wave), Index(e)
                    }, q);

                    if ((i & 3u) == 0u)
                    {
                        StressAlignedBig big{};
                        big.tag = (t << 16) | i;
                        std::memset(big.payload, static_cast<int>((t + i) & 0xFF), sizeof(big.payload));
                        reg->AddComponent(e, big, q);
                    }
                }
            });
        }

        for (auto& w : workers)
            w.join();

        reg->FlushCommands(q);
        VerifyPoolBijection(reg->Pool<StressPos>());
        VerifyPoolBijection(reg->Pool<StressAlignedBig>());
        reg->ClearTombstones();
    }

    // Verify no duplicate handles across all surviving thread lists
    std::vector<Entity> allAlive;
    for (const auto& list : threadEntities)
        allAlive.insert(allAlive.end(), list.begin(), list.end());

    std::sort(allAlive.begin(), allAlive.end());
    CHECK(std::unique(allAlive.begin(), allAlive.end()) == allAlive.end());
    CHECK(reg->Pool<StressPos>().Count() == allAlive.size());

    for (Entity e : allAlive)
    {
        REQUIRE(reg->IsAlive(e));
        const StressPos* p = reg->Pool<StressPos>().Get(Index(e));
        REQUIRE(p != nullptr);
        CHECK(p->id == Index(e));
    }
}

TEST_CASE("ECS Stress: FrameSlab exhaustion & aligned heap-fallback integrity")
{
    auto reg = std::make_unique<EcsRegistry>();
    // Small slab (2 KiB) forces almost all 128-byte aligned allocations onto heap fallback.
    EcsCommandQueue q(2048);

    constexpr uint32_t kCount = 2000;
    std::vector<Entity> entities;
    entities.reserve(kCount);

    for (uint32_t i = 0; i < kCount; ++i)
    {
        Entity e = reg->CreateEntity();
        entities.push_back(e);

        StressAlignedBig big{};
        big.tag = 0xA5000000u | i;
        for (size_t b = 0; b < sizeof(big.payload); ++b)
            big.payload[b] = static_cast<uint8_t>((i + b) & 0xFF);
        reg->AddComponent(e, big, q);
    }

    reg->FlushCommands(q);

    const auto& pool = reg->Pool<StressAlignedBig>();
    REQUIRE(pool.Count() == kCount);
    VerifyPoolBijection(pool);

    for (uint32_t i = 0; i < kCount; ++i)
    {
        const StressAlignedBig* got = pool.Get(Index(entities[i]));
        REQUIRE(got != nullptr);
        CHECK((reinterpret_cast<uintptr_t>(got) % alignof(StressAlignedBig)) == 0);
        CHECK(got->tag == (0xA5000000u | i));
        CHECK(got->payload[0] == static_cast<uint8_t>(i & 0xFF));
        CHECK(got->payload[123] == static_cast<uint8_t>((i + 123) & 0xFF));
    }

    // Verify EcsCommandQueue destructor cleanly frees un-flushed heap payloads
    {
        EcsCommandQueue unFlushedQueue(512);
        for (uint32_t i = 0; i < 100; ++i)
        {
            StressAlignedBig big{};
            big.tag = i;
            reg->AddComponent(entities[i], big, unFlushedQueue);
        }
    }
}

TEST_CASE("ECS Stress: Entity slot retirement after 4095 generation lifetimes")
{
    auto reg = std::make_unique<EcsRegistry>();
    EcsCommandQueue q;

    Entity initial = reg->CreateEntity();
    const uint32_t trackedIndex = Index(initial);
    REQUIRE(Generation(initial) == 0);

    Entity current = initial;
    constexpr uint32_t kGenMax = (1u << GenBits) - 1u; // 4095

    for (uint32_t gen = 0; gen < kGenMax; ++gen)
    {
        reg->DestroyEntity(current, q);
        reg->FlushCommands(q);
        reg->ClearTombstones();

        CHECK(reg->IsAlive(current) == false);

        Entity recycled = reg->CreateEntity();
        CHECK(reg->IsAlive(recycled) == true);
        CHECK(Index(recycled) == trackedIndex);
        CHECK(Generation(recycled) == gen + 1u);
        current = recycled;
    }

    // At GenMax (4095), FinalizeDestroy permanently retires trackedIndex.
    REQUIRE(Generation(current) == kGenMax);
    reg->DestroyEntity(current, q);
    reg->FlushCommands(q);
    reg->ClearTombstones();

    CHECK(reg->IsAlive(current) == false);
    Entity fresh = reg->CreateEntity();
    CHECK(reg->IsAlive(fresh) == true);
    CHECK(Index(fresh) != trackedIndex);
    CHECK(reg->IsAlive(current) == false);
}

TEST_CASE("ECS Stress: Parallel PhaseSchedule dispatch across 50k entities")
{
    BlockingThreadPool& pool = BlockingThreadPool::Get();
    pool.Init(4);

    auto reg = std::make_unique<EcsRegistry>();
    EcsCommandQueue q;

    constexpr uint32_t kEntityCount = 50'000;
    constexpr uint32_t kFrames      = 20;

    for (uint32_t i = 0; i < kEntityCount; ++i)
    {
        Entity e = reg->CreateEntity();
        reg->AddComponent(e, StressPos{ 0.0f, 0.0f, 0.0f, i }, q);
        reg->AddComponent(e, StressVel{ 1.0f, 2.0f, 3.0f }, q);
        reg->AddComponent(e, StressTag{ 0u }, q);
    }
    reg->FlushCommands(q);

    CachedView<StressPos, StressVel> cachedPosVel(*reg);
    std::atomic<uint64_t> syncCheckSum{ 0 };

    std::vector<SystemDescriptor> systems;

    SystemDescriptor integratePos;
    integratePos.name          = "StressIntegratePos";
    integratePos.phase         = SchedulePhase::PrePhysics;
    integratePos.affinity      = ThreadAffinity::AnyThread;
    integratePos.access.reads  = MaskOf<StressVel>();
    integratePos.access.writes = MaskOf<StressPos>();
    integratePos.execute       = [](EcsRegistry& r, EcsCommandQueue&, float dt, uint64_t epoch) {
        const auto& velPool = r.Pool<StressVel>();
        auto posWrite = r.Pool<StressPos>().WriteView(epoch, WriteSource::Gameplay);
        const uint32_t n = posWrite.Count();
        for (uint32_t i = 0; i < n; ++i)
        {
            const uint32_t idx = posWrite.entities[i];
            const StressVel* v = velPool.Get(idx);
            if (!v) continue;
            posWrite.data[i].x += v->vx * dt;
            posWrite.data[i].y += v->vy * dt;
            posWrite.data[i].z += v->vz * dt;
            posWrite.MarkDirty(i);
        }
    };
    systems.push_back(std::move(integratePos));

    SystemDescriptor bumpTag;
    bumpTag.name          = "StressBumpTag";
    bumpTag.phase         = SchedulePhase::PrePhysics;
    bumpTag.affinity      = ThreadAffinity::AnyThread;
    bumpTag.access.reads  = MaskOf<StressTag>();
    bumpTag.access.writes = MaskOf<StressTag>();
    bumpTag.execute       = [](EcsRegistry& r, EcsCommandQueue&, float, uint64_t epoch) {
        auto tagWrite = r.Pool<StressTag>().WriteView(epoch, WriteSource::System);
        const uint32_t n = tagWrite.Count();
        for (uint32_t i = 0; i < n; ++i)
        {
            tagWrite.data[i].flags += 1u;
            tagWrite.MarkDirty(i);
        }
    };
    systems.push_back(std::move(bumpTag));

    SystemDescriptor verifyLayer2;
    verifyLayer2.name          = "StressVerifyLayer2";
    verifyLayer2.phase         = SchedulePhase::PrePhysics;
    verifyLayer2.affinity      = ThreadAffinity::AnyThread;
    verifyLayer2.access.reads  = MaskOf<StressPos, StressTag>();
    verifyLayer2.execute       = [&](EcsRegistry& r, EcsCommandQueue&, float, uint64_t) {
        const auto posRead = r.Pool<StressPos>().ReadView();
        syncCheckSum.fetch_add(posRead.Count(), std::memory_order_relaxed);
    };
    systems.push_back(std::move(verifyLayer2));

    SystemDescriptor renderSync;
    renderSync.name         = "StressRenderSync";
    renderSync.phase        = SchedulePhase::RenderSync;
    renderSync.affinity     = ThreadAffinity::MainThread;
    renderSync.access.reads = MaskOf<StressPos, StressVel>();
    renderSync.execute      = [&](EcsRegistry& r, EcsCommandQueue&, float, uint64_t) {
        const auto& posPool = r.Pool<StressPos>();
        uint64_t count = 0;
        cachedPosVel.Each([&](uint32_t entityIdx) {
            if (posPool.Get(entityIdx) != nullptr)
                ++count;
        });
        syncCheckSum.fetch_add(count, std::memory_order_relaxed);
    };
    systems.push_back(std::move(renderSync));

    const PhaseSchedule schedule = BuildPhaseSchedule(systems);
    REQUIRE(schedule.LayersFor(SchedulePhase::PrePhysics).size() == 2);
    REQUIRE(schedule.LayersFor(SchedulePhase::PrePhysics)[0].systems.size() == 2);
    REQUIRE(schedule.LayersFor(SchedulePhase::RenderSync).size() == 1);

    const uint64_t epochBefore = reg->CurrentEpoch();
    for (uint32_t frame = 0; frame < kFrames; ++frame)
    {
        DispatchPhase(schedule, SchedulePhase::PrePhysics, *reg, q, pool, 1.0f);
        reg->FlushCommands(q);
        DispatchPhase(schedule, SchedulePhase::RenderSync, *reg, q, pool, 1.0f);
        reg->ClearTombstones();
    }

    CHECK(reg->CurrentEpoch() > epochBefore);
    CHECK(syncCheckSum.load(std::memory_order_relaxed) == static_cast<uint64_t>(kFrames) * kEntityCount * 2ULL);

    const auto posRead = reg->Pool<StressPos>().ReadView();
    const auto tagRead = reg->Pool<StressTag>().ReadView();
    REQUIRE(posRead.Count() == kEntityCount);
    REQUIRE(tagRead.Count() == kEntityCount);
    CHECK(posRead.data[0].x == doctest::Approx(static_cast<float>(kFrames)));
    CHECK(posRead.data[kEntityCount - 1].y == doctest::Approx(static_cast<float>(kFrames) * 2.0f));
    CHECK(tagRead.data[0].flags == kFrames);
    CHECK(tagRead.data[kEntityCount - 1].flags == kFrames);
}

TEST_CASE("ECS: TryPool returns nullptr before pool creation and valid pointer afterward")
{
    auto reg = std::make_unique<EcsRegistry>();
    const EcsRegistry& constReg = *reg;

    CHECK(reg->TryPool<StressCounter>() == nullptr);
    CHECK(constReg.TryPool<StressCounter>() == nullptr);

    EcsCommandQueue q;
    Entity e = reg->CreateEntity();
    reg->AddComponent(e, StressCounter{ 42u }, q);

    // Pool is created on AddComponent (before FlushCommands), initially empty
    REQUIRE(reg->TryPool<StressCounter>() != nullptr);
    REQUIRE(constReg.TryPool<StressCounter>() != nullptr);
    CHECK(constReg.TryPool<StressCounter>()->Count() == 0);

    reg->FlushCommands(q);
    CHECK(constReg.TryPool<StressCounter>()->Count() == 1);
    CHECK(constReg.TryPool<StressCounter>()->Get(Index(e))->ticks == 42u);

#ifdef ZERO_ENABLE_ASSERTS
    // Verify SystemAccessView masks for read/write validation (including high bit >= 64)
    Detail::SystemAccessView access{};
    access.readLo  = MaskOf<StressPos>().lo;
    access.readHi  = MaskOf<StressPos>().hi;
    access.writeLo = MaskOf<StressCounter>().lo;
    access.writeHi = MaskOf<StressCounter>().hi;

    CHECK(access.CanRead(ComponentId<StressPos>()) == true);
    CHECK(access.CanWrite(ComponentId<StressPos>()) == false);
    CHECK(access.CanRead(ComponentId<StressCounter>()) == true);
    CHECK(access.CanWrite(ComponentId<StressCounter>()) == true);
    CHECK(access.CanRead(ComponentId<StressVel>()) == false);
    CHECK(access.CanWrite(ComponentId<StressVel>()) == false);
#endif
}
