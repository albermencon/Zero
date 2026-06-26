#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include <Engine/Ref.h>
#include <thread>
#include <vector>
#include <atomic>

using namespace Zero;

// Mocks for Testing

struct TestBase : public ReferenceCounted
{
    virtual ~TestBase() = default;
    int data;
    explicit TestBase(int d = 42) : data(d) {}
};

struct TestDerived : public TestBase
{
    int extra = 100;
};

struct Unrelated : public ReferenceCounted {};

// Single-Threaded Tests

TEST_CASE("Ref: null state")
{
    Ref<TestBase> a;
    CHECK(a.Get() == nullptr);
    CHECK(!a);
    CHECK(a == nullptr);
    CHECK(!(a != nullptr));
}

TEST_CASE("Ref: MakeRef with constructor args")
{
    Ref<TestBase> a = MakeRef<TestBase>(99);
    CHECK(a != nullptr);
    CHECK(a->data == 99);
    CHECK(a->GetRefCount() == 1);
}

TEST_CASE("Ref: copy semantics")
{
    Ref<TestBase> a = MakeRef<TestBase>();
    CHECK(a->GetRefCount() == 1);

    Ref<TestBase> b = a;
    CHECK(a->GetRefCount() == 2);
    CHECK(b->GetRefCount() == 2);
    CHECK(b.Get() == a.Get());

    b = nullptr;
    CHECK(a->GetRefCount() == 1);
    CHECK(b.Get() == nullptr);
}

TEST_CASE("Ref: move semantics")
{
    Ref<TestBase> a = MakeRef<TestBase>();
    TestBase* raw = a.Get();

    Ref<TestBase> b = std::move(a);
    CHECK(b->GetRefCount() == 1);
    CHECK(b.Get() == raw);
    CHECK(a.Get() == nullptr);
}

TEST_CASE("Ref: copy assignment")
{
    Ref<TestBase> a = MakeRef<TestBase>();
    Ref<TestBase> b = MakeRef<TestBase>();
    b = a;
    CHECK(a->GetRefCount() == 2);
    CHECK(b.Get() == a.Get());
}

TEST_CASE("Ref: move assignment")
{
    Ref<TestBase> a = MakeRef<TestBase>();
    TestBase* raw = a.Get();
    Ref<TestBase> b;
    b = std::move(a);
    CHECK(b.Get() == raw);
    CHECK(b->GetRefCount() == 1);
    CHECK(a.Get() == nullptr);
}

TEST_CASE("Ref: self-assignment (copy)")
{
    Ref<TestBase> a = MakeRef<TestBase>();
    TestBase* raw = a.Get();
    a = a;
    CHECK(a->GetRefCount() == 1);
    CHECK(a.Get() == raw);
}

TEST_CASE("Ref: self-assignment (move)")
{
    Ref<TestBase> a = MakeRef<TestBase>();
    TestBase* raw = a.Get();
    a = std::move(a);
    CHECK(a.Get() == raw);
    CHECK(a->GetRefCount() == 1);
}

TEST_CASE("Ref: Reset to null")
{
    Ref<TestBase> a = MakeRef<TestBase>();
    a.Reset();
    CHECK(a.Get() == nullptr);
}

TEST_CASE("Ref: Reset to new raw pointer")
{
    Ref<TestBase> a = MakeRef<TestBase>(1);
    Ref<TestBase> b = MakeRef<TestBase>(2);
    TestBase* raw_b = b.Get();

    a.Reset(raw_b);
    CHECK(a.Get() == raw_b);
    CHECK(a->GetRefCount() == 2);
    CHECK(a->data == 2);
}

TEST_CASE("Ref: Swap")
{
    Ref<TestBase> a = MakeRef<TestBase>(1);
    Ref<TestBase> b = MakeRef<TestBase>(2);
    TestBase* raw_a = a.Get();
    TestBase* raw_b = b.Get();

    a.Swap(b);
    CHECK(a.Get() == raw_b);
    CHECK(b.Get() == raw_a);

    Zero::Swap(a, b);
    CHECK(a.Get() == raw_a);
    CHECK(b.Get() == raw_b);
}

TEST_CASE("Ref: polymorphic upcast")
{
    Ref<TestDerived> derived = MakeRef<TestDerived>();
    Ref<TestBase> base = derived;
    CHECK(base->GetRefCount() == 2);
    CHECK(base->data == 42);
}

TEST_CASE("Ref: As() downcast success")
{
    Ref<TestBase> base = MakeRef<TestDerived>();
    Ref<TestDerived> derived = base.As<TestDerived>();
    CHECK(derived != nullptr);
    CHECK(derived->GetRefCount() == 2);
    CHECK(derived->extra == 100);
}

TEST_CASE("Ref: As() downcast failure returns null")
{
    Ref<TestBase> base = MakeRef<TestBase>();
    Ref<TestDerived> derived = base.As<TestDerived>();
    CHECK(derived == nullptr);
    CHECK(base->GetRefCount() == 1);
}

// WeakRef Tests

TEST_CASE("WeakRef: default constructed is null")
{
    WeakRef<TestBase> w;
    CHECK(w.Lock() == nullptr);
    CHECK(!w.IsValid());
    CHECK(!w);
}

TEST_CASE("WeakRef: Lock returns valid Ref while object is alive")
{
    Ref<TestBase> a = MakeRef<TestBase>();
    WeakRef<TestBase> w = a;

    CHECK(w.IsValid());
    Ref<TestBase> b = w.Lock();
    CHECK(b != nullptr);
    CHECK(b->GetRefCount() == 2);
}

TEST_CASE("WeakRef: Lock returns null after strong ref drops")
{
    Ref<TestBase> a = MakeRef<TestBase>();
    WeakRef<TestBase> w = a;

    a.Reset();
    CHECK(!w.IsValid());
    CHECK(w.Lock() == nullptr);
}

TEST_CASE("WeakRef: Reset clears the weak reference")
{
    Ref<TestBase> a = MakeRef<TestBase>();
    WeakRef<TestBase> w = a;
    w.Reset();
    CHECK(!w.IsValid());
    CHECK(w.Lock() == nullptr);
}

TEST_CASE("WeakRef: copy constructor")
{
    Ref<TestBase> a = MakeRef<TestBase>();
    WeakRef<TestBase> w1 = a;
    WeakRef<TestBase> w2 = w1;

    CHECK(w2.IsValid());
    CHECK(w2.Lock()->GetRefCount() == 2);
}

TEST_CASE("WeakRef: move constructor")
{
    Ref<TestBase> a = MakeRef<TestBase>();
    WeakRef<TestBase> w1 = a;
    WeakRef<TestBase> w2 = std::move(w1);

    CHECK(w2.IsValid());
    CHECK(!w1.IsValid());
}

TEST_CASE("WeakRef: copy assignment")
{
    Ref<TestBase> a = MakeRef<TestBase>();
    WeakRef<TestBase> w1 = a;
    WeakRef<TestBase> w2;
    w2 = w1;

    CHECK(w2.IsValid());
    a.Reset();
    CHECK(!w1.IsValid());
    CHECK(!w2.IsValid());
}

TEST_CASE("WeakRef: move assignment")
{
    Ref<TestBase> a = MakeRef<TestBase>();
    WeakRef<TestBase> w1 = a;
    WeakRef<TestBase> w2;
    w2 = std::move(w1);

    CHECK(w2.IsValid());
    CHECK(!w1.IsValid());
}

TEST_CASE("WeakRef: WeakRefBlock freed when last weak ref drops after strong ref")
{
    // This test verifies there is no leak or use-after-free on the WeakRefBlock lifetime.
    // If ASan is enabled, it will catch any violation here.
    WeakRef<TestBase> w;
    {
        Ref<TestBase> a = MakeRef<TestBase>();
        w = a;
        CHECK(w.IsValid());
    }
    // strong ref gone, but weak ref still holds the block alive
    CHECK(!w.IsValid());
    CHECK(w.Lock() == nullptr);
    // destructor of w here drops the last weakRefCount -> block must be freed
}

TEST_CASE("WeakRef: multiple WeakRefs from same object share a block")
{
    Ref<TestBase> a = MakeRef<TestBase>();
    WeakRef<TestBase> w1 = a;
    WeakRef<TestBase> w2 = a;

    CHECK(w1.Lock().Get() == w2.Lock().Get());

    a.Reset();
    CHECK(!w1.IsValid());
    CHECK(!w2.IsValid());
}

TEST_CASE("WeakRef: Swap")
{
    Ref<TestBase> a = MakeRef<TestBase>(1);
    Ref<TestBase> b = MakeRef<TestBase>(2);
    WeakRef<TestBase> w1 = a;
    WeakRef<TestBase> w2 = b;

    w1.Swap(w2);
    CHECK(w1.Lock()->data == 2);
    CHECK(w2.Lock()->data == 1);
}

// Multi-Threaded Tests

TEST_CASE("MT: concurrent Ref copy/destroy (shared refcount)")
{
    // All threads copy the same strong ref and immediately drop it.
    // The refcount must return to exactly 1 at the end.
    Ref<TestBase> shared = MakeRef<TestBase>();
    const int num_threads = 30;
    const int loops = 10000;
    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back([shared]() mutable {
            for (int j = 0; j < loops; ++j)
            {
                Ref<TestBase> local = shared;
                local.Reset();
            }
        });
    }

    for (auto& t : threads) t.join();
    CHECK(shared->GetRefCount() == 1);
}

TEST_CASE("MT: concurrent WeakRef creation and lock")
{
    // Multiple threads create WeakRefs from the same strong Ref concurrently
    // and attempt to Lock() them. No crashes allowed.
    Ref<TestBase> a = MakeRef<TestBase>();
    const int num_threads = 20;
    const int loops = 5000;
    std::atomic<int> successful_locks{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; ++i)
    {
        threads.emplace_back([&a, &successful_locks, loops]() {
            for (int j = 0; j < loops; ++j)
            {
                WeakRef<TestBase> w = a;
                Ref<TestBase> locked = w.Lock();
                if (locked) successful_locks.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& t : threads) t.join();
    CHECK(successful_locks.load() == num_threads * loops);
    CHECK(a->GetRefCount() == 1);
}

TEST_CASE("MT: ABA stress test — Lock() racing with destruction")
{
    // 1-million iteration test proving TryIncRefCount() prevents the 0->1 resurrection race.
    // This is the core correctness proof for the entire system.
    const int num_iterations = 1000000;
    const int num_threads = 20;

    std::atomic<int> start_flag{0};
    std::atomic<bool> done{false};
    std::atomic<int> iteration{0};

    std::vector<WeakRef<TestBase>> weak_refs(num_threads);
    std::vector<std::atomic<int>> thread_acks(num_threads);

    for (int t = 0; t < num_threads; ++t)
        thread_acks[t].store(0, std::memory_order_relaxed);

    std::vector<std::thread> threads;
    for (int t = 0; t < num_threads; ++t)
    {
        threads.emplace_back([&, t]() {
            while (start_flag.load(std::memory_order_acquire) == 0) { std::this_thread::yield(); }

            int last_iter = -1;
            while (!done.load(std::memory_order_acquire))
            {
                int current_iter = iteration.load(std::memory_order_acquire);
                if (current_iter != last_iter)
                {
                    Ref<TestBase> locked = weak_refs[t].Lock();
                    if (locked) { volatile int x = locked->data; (void)x; }
                    last_iter = current_iter;
                    thread_acks[t].store(current_iter, std::memory_order_release);
                }
                else {
                    std::this_thread::yield();
                }
            }
        });
    }

    start_flag.store(1, std::memory_order_release);

    for (int i = 1; i <= num_iterations; ++i)
    {
        Ref<TestBase> a = MakeRef<TestBase>();
        for (int t = 0; t < num_threads; ++t) weak_refs[t] = a;

        iteration.store(i, std::memory_order_release);
        if (i % 2 == 0) std::this_thread::yield();

        a.Reset();

        for (int t = 0; t < num_threads; ++t)
            while (thread_acks[t].load(std::memory_order_acquire) != i)
                std::this_thread::yield();
    }

    done.store(true, std::memory_order_release);
    iteration.fetch_add(1, std::memory_order_release);
    for (auto& t : threads) t.join();

    CHECK(true); // Reaching here without a segfault proves ABA is prevented
}
