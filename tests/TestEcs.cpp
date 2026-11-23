#include <doctest.h>
#include <Engine/Ecs/EcsRegistry.h>
#include <Engine/Ecs/Entity.h>
#include <Engine/Ecs/ComponentTrait.h>
#include <Engine/Ecs/ComponentMask.h>
#include <Engine/Ecs/EcsMask.h>
#include <Engine/Ecs/EcsCommandQueue.h>
#include <Engine/Ecs/ComponentPool.h>
#include <Engine/Ecs/CachedView.h>
#include <Engine/Ecs/Systems/RenderSyncSystem.h>
#include <Engine/Scene/SceneManager.h>
#include <Engine/Scene/Scene.h>
#include <Engine/Ecs/Scheduler.h>
#include <Engine/Ecs/FrameSlab.h>
#include <thread>
#include <vector>
#include <atomic>

using namespace Zero;

// Dummy components for testing
struct TestCompA { int val; };
struct TestCompB { float val; };
struct TestCompC { double val; };
static_assert(std::is_trivially_copyable_v<TestCompA>);
static_assert(std::is_trivially_copyable_v<TestCompB>);
static_assert(std::is_trivially_copyable_v<TestCompC>);

REGISTER_COMPONENT(TestCompA, 100)
REGISTER_COMPONENT(TestCompB, 101)
REGISTER_COMPONENT(TestCompC, 102)

TEST_CASE("ECS: Registration & ID uniqueness")
{
    CHECK(ComponentId<TestCompA>() == 100);
    CHECK(ComponentId<TestCompB>() == 101);
    CHECK(ComponentId<TestCompC>() == 102);
}

TEST_CASE("ECS: ComponentMask operations")
{
    ComponentMask mask;
    mask.Set(100);
    mask.Set(101);

    CHECK(mask.Has(100) == true);
    CHECK(mask.Has(101) == true);
    CHECK(mask.Has(102) == false);

    mask.Clear(100);
    CHECK(mask.Has(100) == false);
    CHECK(mask.Has(101) == true);

    ComponentMask mask2 = MaskOf<TestCompB, TestCompC>();
    CHECK(mask2.Has(101) == true);
    CHECK(mask2.Has(102) == true);
    CHECK(mask2.Has(100) == false);
}

TEST_CASE("ECS: EntityAllocator (Liveness and Generation)")
{
    EcsRegistry reg;
    Entity e1 = reg.CreateEntity();
    CHECK(reg.IsAlive(e1) == true);

    EcsCommandQueue q;
    reg.DestroyEntity(e1, q);

    // Still alive until flush
    CHECK(reg.IsAlive(e1) == true);

    reg.FlushCommands(q);

    // Dead after flush
    CHECK(reg.IsAlive(e1) == false);

    // Create again - index should be recycled with bumped generation
    Entity e2 = reg.CreateEntity();
    CHECK(reg.IsAlive(e2) == true);
    CHECK(Index(e2) == Index(e1));
    CHECK(Generation(e2) == Generation(e1) + 1);
}

TEST_CASE("ECS: Registry Add/Remove/Destroy")
{
    EcsRegistry reg;
    EcsCommandQueue q;
    Entity e1 = reg.CreateEntity();

    reg.AddComponent(e1, TestCompA{42}, q);
    reg.FlushCommands(q);

    CHECK(reg.Mask(Index(e1)).Has(ComponentId<TestCompA>()) == true);
    auto& poolA = reg.Pool<TestCompA>();
    CHECK(poolA.Count() == 1);
    CHECK(poolA.Get(Index(e1))->val == 42);

    reg.RemoveComponent<TestCompA>(e1, q);
    reg.FlushCommands(q);

    CHECK(reg.Mask(Index(e1)).Has(ComponentId<TestCompA>()) == false);
    CHECK(poolA.Count() == 0);
    CHECK(poolA.Get(Index(e1)) == nullptr);
}

TEST_CASE("ECS: CachedView RAII & Batched invalidation")
{
    EcsRegistry reg;
    EcsCommandQueue q;

    Entity e1 = reg.CreateEntity();
    reg.AddComponent(e1, TestCompA{1}, q);
    reg.FlushCommands(q);

    {
        CachedView<TestCompA> view(reg);
        
        uint32_t count = 0;
        view.Each([&](uint32_t) { count++; });
        CHECK(count == 1);

        // Modify structure
        Entity e2 = reg.CreateEntity();
        reg.AddComponent(e2, TestCompA{2}, q);
        reg.FlushCommands(q);

        count = 0;
        view.Each([&](uint32_t) { count++; });
        CHECK(count == 2);
    } // view out of scope - unregistered

    // Should not crash when flushing after view is destroyed
    Entity e3 = reg.CreateEntity();
    reg.AddComponent(e3, TestCompA{3}, q);
    reg.FlushCommands(q);
}

TEST_CASE("ECS: FrameSlab Alignment")
{
    uint8_t dummyData[16] = {0};
    FrameSlab slab(1024, 64);
    uint32_t off1 = slab.Allocate(dummyData, 16, 64);
    uint32_t off2 = slab.Allocate(dummyData, 16, 64);
    
    CHECK((off1 % 64) == 0);
    CHECK((off2 % 64) == 0);
    CHECK(off2 >= off1 + 64);
}

TEST_CASE("ECS: Scheduler Execution Layers")
{
    using ExecFn = void(*)(EcsRegistry&, EcsCommandQueue&, float, uint64_t);
    SystemDescriptor sys1 { "Sys1", { MaskOf<TestCompA>(), MaskOf<TestCompB>() }, static_cast<ExecFn>(nullptr) };
    SystemDescriptor sys2 { "Sys2", { MaskOf<TestCompC>(), MaskOf<TestCompA>() }, static_cast<ExecFn>(nullptr) };
    SystemDescriptor sys3 { "Sys3", { MaskOf<TestCompB>(), MaskOf<TestCompC>() }, static_cast<ExecFn>(nullptr) };

    std::vector<SystemDescriptor> systems = { sys1, sys2, sys3 };
    auto layers = BuildExecutionLayers(systems);

    // Sys1 writes CompB, Sys2 reads CompA (written by nobody here, wait Sys1 reads CompA, writes CompB).
    // Sys2 reads CompC, writes CompA.
    // Sys1 reads A (Sys2 writes A -> conflict)
    // Sys3 reads B (Sys1 writes B -> conflict), writes C (Sys2 reads C -> conflict)
    // They should all conflict and form 3 sequential layers.
    CHECK(layers.size() == 3);
}

TEST_CASE("ECS: MPMC Free List Concurrent Minting")
{
    EcsRegistry reg;
    constexpr uint32_t NumThreads = 4;
    constexpr uint32_t EntitiesPerThread = 10000;
    
    std::vector<std::thread> threads;
    std::vector<std::vector<Entity>> results(NumThreads);
    
    for (uint32_t i = 0; i < NumThreads; ++i)
    {
        threads.emplace_back([&, i]() {
            for (uint32_t j = 0; j < EntitiesPerThread; ++j)
                results[i].push_back(reg.CreateEntity());
        });
    }
    
    for (auto& t : threads) t.join();
    
    // Verify no duplicate entities were minted
    std::vector<Entity> allEntities;
    for (const auto& res : results)
        allEntities.insert(allEntities.end(), res.begin(), res.end());
        
    std::sort(allEntities.begin(), allEntities.end());
    auto it = std::unique(allEntities.begin(), allEntities.end());
    CHECK(it == allEntities.end()); // No duplicates found
}

TEST_CASE("ECS: Pool Pointer Stability & No Realloc")
{
    EcsRegistry reg;
    EcsCommandQueue q;
    Entity e = reg.CreateEntity();
    
    reg.AddComponent(e, TestCompA{1}, q);
    reg.FlushCommands(q);
    
    ComponentPool<TestCompA>& pool = reg.Pool<TestCompA>();
    const TestCompA* ptr = pool.Get(Index(e));
    
    // Simulating adding 100 more pools doesn't invalidate the pointer because m_pools is a fixed array of unique_ptr
    reg.AddComponent(e, TestCompB{2.0f}, q);
    reg.AddComponent(e, TestCompC{3.0}, q);
    reg.FlushCommands(q);
    
    CHECK(pool.Get(Index(e)) == ptr);
}

TEST_CASE("ECS: GetMutable Dirty Marking")
{
    EcsRegistry reg;
    EcsCommandQueue q;
    Entity e = reg.CreateEntity();
    
    reg.AddComponent(e, TestCompA{10}, q);
    const uint64_t flushEpoch = reg.FlushCommands(q);
    
    auto& pool = reg.Pool<TestCompA>();
    CHECK(pool.ModifiedEpochOf(Index(e)) == flushEpoch);
    CHECK(pool.ModifiedSourceOf(Index(e)) == WriteSource::Command);
    
    const uint64_t nextEpoch = reg.AdvanceEpoch();
    {
        auto mutableRef = pool.GetMutable(Index(e), nextEpoch, WriteSource::Gameplay);
        mutableRef->val = 20;
    } // DirtyRef destructor sets modifiedEpoch & modifiedSource
    
    CHECK(pool.ModifiedEpochOf(Index(e)) == nextEpoch);
    CHECK(pool.ModifiedSourceOf(Index(e)) == WriteSource::Gameplay);
    CHECK(pool.Get(Index(e))->val == 20);
}


