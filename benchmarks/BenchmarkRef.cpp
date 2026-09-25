#include <benchmark/benchmark.h>
#include <Engine/Ref.h>

namespace
{
    struct BenchmarkObject : public Zero::ReferenceCounted
    {
        uint64_t data[4]{};
    };

    void BM_Ref_CreateDestroy(benchmark::State& state)
    {
        for (auto _ : state)
        {
            Zero::Ref<BenchmarkObject> ref = Zero::MakeRef<BenchmarkObject>();
            benchmark::DoNotOptimize(ref);
        }
    }
    BENCHMARK(BM_Ref_CreateDestroy);

    void BM_Ref_Copy(benchmark::State& state)
    {
        Zero::Ref<BenchmarkObject> source = Zero::MakeRef<BenchmarkObject>();
        for (auto _ : state)
        {
            Zero::Ref<BenchmarkObject> copy = source;
            benchmark::DoNotOptimize(copy);
        }
    }
    BENCHMARK(BM_Ref_Copy);

    void BM_WeakRef_Upgrade(benchmark::State& state)
    {
        Zero::Ref<BenchmarkObject> strong = Zero::MakeRef<BenchmarkObject>();
        Zero::WeakRef<BenchmarkObject> weak = strong;
        for (auto _ : state)
        {
            Zero::Ref<BenchmarkObject> locked = weak.Lock();
            benchmark::DoNotOptimize(locked);
        }
    }
    BENCHMARK(BM_WeakRef_Upgrade);
}
