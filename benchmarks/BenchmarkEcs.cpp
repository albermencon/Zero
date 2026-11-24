#include <benchmark/benchmark.h>
#include <Engine/Ecs/CachedView.h>
#include <Engine/Ecs/ComponentPool.h>
#include <Engine/Ecs/ComponentTrait.h>
#include <Engine/Ecs/ComponentView.h>
#include <Engine/Ecs/EcsCommandQueue.h>
#include <Engine/Ecs/EcsRegistry.h>
#include <Engine/Ecs/Entity.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

namespace
{
	struct BenchPos
	{
		float x, y, z;
	};

	struct BenchVel
	{
		float vx, vy, vz;
	};

	struct BenchTag
	{
		uint32_t flags;
	};

	struct alignas(32) BenchLarge
	{
		uint32_t tag;
		uint8_t payload[124];
	};

	static_assert(std::is_trivially_copyable_v<BenchPos>);
	static_assert(std::is_trivially_copyable_v<BenchVel>);
	static_assert(std::is_trivially_copyable_v<BenchTag>);
	static_assert(std::is_trivially_copyable_v<BenchLarge>);
}
REGISTER_COMPONENT(BenchPos, 120)
REGISTER_COMPONENT(BenchVel, 121)
REGISTER_COMPONENT(BenchTag, 122)
REGISTER_COMPONENT(BenchLarge, 123)

namespace
{
	// Shared registry for multi-threaded entity allocation benchmarks.
	std::unique_ptr<Zero::EcsRegistry> g_parallelReg;

	// Entity Allocation

	void BM_ECS_MintEntity_Sequential(benchmark::State& state)
	{
		constexpr uint32_t kMaxLivePerRegistry = 500'000;
		auto reg = std::make_unique<Zero::EcsRegistry>();
		uint32_t liveCount = 0;

		for (auto _ : state)
		{
			if (liveCount == kMaxLivePerRegistry)
			{
				state.PauseTiming();
				reg = std::make_unique<Zero::EcsRegistry>();
				liveCount = 0;
				state.ResumeTiming();
			}

			Zero::Entity e = reg->CreateEntity();
			benchmark::DoNotOptimize(e);
			++liveCount;
		}

		state.SetItemsProcessed(state.iterations());
	}
	BENCHMARK(BM_ECS_MintEntity_Sequential)->Unit(benchmark::kNanosecond);

	void BM_ECS_MintEntity_Parallel(benchmark::State& state)
	{
		constexpr uint64_t kMaxPrewarm = 900'000;

		if (state.thread_index() == 0)
		{
			g_parallelReg = std::make_unique<Zero::EcsRegistry>();
			Zero::EcsCommandQueue q;

			// Pre-populate the lock-free free list so threads contend on MPMC try_dequeue.
			const uint64_t requested = static_cast<uint64_t>(state.threads()) *
			                           static_cast<uint64_t>(state.max_iterations);
			const uint32_t toRecycle = static_cast<uint32_t>(std::min(requested, kMaxPrewarm));

			std::vector<Zero::Entity> batch;
			batch.reserve(50'000);
			uint32_t remaining = toRecycle;
			while (remaining > 0)
			{
				const uint32_t n = std::min<uint32_t>(remaining, 50'000);
				batch.clear();
				for (uint32_t i = 0; i < n; ++i)
					batch.push_back(g_parallelReg->CreateEntity());
				for (uint32_t i = 0; i < n; ++i)
					g_parallelReg->DestroyEntity(batch[i], q);
				g_parallelReg->FlushCommands(q);
				g_parallelReg->ClearTombstones();
				remaining -= n;
			}
		}

		for (auto _ : state)
		{
			Zero::Entity e = g_parallelReg->CreateEntity();
			benchmark::DoNotOptimize(e);
		}

		if (state.thread_index() == 0)
		{
			g_parallelReg.reset();
		}

		state.SetItemsProcessed(state.iterations());
	}
	BENCHMARK(BM_ECS_MintEntity_Parallel)
		->Threads(2)
		->Threads(4)
		->Threads(8)
		->ThreadRange(1, 16)
		->Iterations(50'000)
		->Unit(benchmark::kNanosecond)
		->UseRealTime();

	// Deferred Command Pipeline

	void BM_ECS_EnqueueAdd(benchmark::State& state)
	{
		auto reg = std::make_unique<Zero::EcsRegistry>();
		Zero::EcsCommandQueue q;
		const Zero::Entity e = reg->CreateEntity();
		// Pre-warm the pool so GetOrCreatePool inside AddComponent takes the fast atomic path.
		(void)reg->Pool<BenchPos>();

		const BenchPos pos{ 1.0f, 2.0f, 3.0f };
		static_assert(sizeof(BenchPos) <= 44, "Expected inline EcsCommand payload");

		constexpr uint32_t kDrainInterval = 65'536;
		uint32_t queued = 0;

		for (auto _ : state)
		{
			if (queued == kDrainInterval)
			{
				state.PauseTiming();
				Zero::EcsCommand cmd;
				while (q.TryDequeue(cmd)) {}
				queued = 0;
				state.ResumeTiming();
			}

			reg->AddComponent(e, pos, q);
			++queued;
		}

		state.SetItemsProcessed(state.iterations());
		state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(sizeof(BenchPos)));
	}
	BENCHMARK(BM_ECS_EnqueueAdd)->Unit(benchmark::kNanosecond);

	void BM_ECS_FlushCommands_Insert10k(benchmark::State& state)
	{
		constexpr uint32_t kCount = 10'000;
		auto reg = std::make_unique<Zero::EcsRegistry>();
		Zero::EcsCommandQueue q;

		std::vector<Zero::Entity> entities;
		entities.reserve(kCount);
		for (uint32_t i = 0; i < kCount; ++i)
			entities.push_back(reg->CreateEntity());

		// Pre-warm pool capacity and command queue blocks before timing.
		for (uint32_t i = 0; i < kCount; ++i)
			reg->AddComponent(entities[i], BenchPos{ static_cast<float>(i), 1.0f, 2.0f }, q);
		reg->FlushCommands(q);
		for (uint32_t i = 0; i < kCount; ++i)
			reg->RemoveComponent<BenchPos>(entities[i], q);
		reg->FlushCommands(q);
		reg->ClearTombstones();

		bool populated = false;
		for (auto _ : state)
		{
			state.PauseTiming();
			if (populated)
			{
				for (uint32_t i = 0; i < kCount; ++i)
					reg->RemoveComponent<BenchPos>(entities[i], q);
				reg->FlushCommands(q);
				reg->ClearTombstones();
			}
			for (uint32_t i = 0; i < kCount; ++i)
				reg->AddComponent(entities[i], BenchPos{ static_cast<float>(i), 1.0f, 2.0f }, q);
			populated = true;
			state.ResumeTiming();

			uint64_t epoch = reg->FlushCommands(q);
			benchmark::DoNotOptimize(epoch);
		}

		state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(kCount));
	}
	BENCHMARK(BM_ECS_FlushCommands_Insert10k)->Unit(benchmark::kNanosecond);

	void BM_ECS_FlushCommands_Destroy10k(benchmark::State& state)
	{
		constexpr uint32_t kCount = 10'000;
		auto reg = std::make_unique<Zero::EcsRegistry>();
		Zero::EcsCommandQueue q;
		std::vector<Zero::Entity> entities(kCount);

		// Pre-warm ComponentPool and tombstone capacities.
		for (uint32_t i = 0; i < kCount; ++i)
		{
			entities[i] = reg->CreateEntity();
			reg->AddComponent(entities[i], BenchPos{ static_cast<float>(i), 1.0f, 2.0f }, q);
		}
		reg->FlushCommands(q);
		for (uint32_t i = 0; i < kCount; ++i)
			reg->DestroyEntity(entities[i], q);
		reg->FlushCommands(q);
		reg->ClearTombstones();

		uint32_t cycle = 0;
		for (auto _ : state)
		{
			state.PauseTiming();
			// Prevent 12-bit generation exhaustion if run for many iterations.
			if (++cycle == 2000)
			{
				reg = std::make_unique<Zero::EcsRegistry>();
				cycle = 0;
			}
			for (uint32_t i = 0; i < kCount; ++i)
			{
				entities[i] = reg->CreateEntity();
				reg->AddComponent(entities[i], BenchPos{ static_cast<float>(i), 1.0f, 2.0f }, q);
			}
			reg->FlushCommands(q);
			for (uint32_t i = 0; i < kCount; ++i)
				reg->DestroyEntity(entities[i], q);
			state.ResumeTiming();

			uint64_t epoch = reg->FlushCommands(q);
			benchmark::DoNotOptimize(epoch);

			state.PauseTiming();
			reg->ClearTombstones();
			state.ResumeTiming();
		}

		state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(kCount));
	}
	BENCHMARK(BM_ECS_FlushCommands_Destroy10k)->Unit(benchmark::kNanosecond);

	// Iteration & Query Throughput

	void BM_ECS_SinglePool_LinearIteration(benchmark::State& state)
	{
		const uint32_t count = static_cast<uint32_t>(state.range(0));
		auto reg = std::make_unique<Zero::EcsRegistry>();
		Zero::EcsCommandQueue q;

		for (uint32_t i = 0; i < count; ++i)
		{
			const Zero::Entity e = reg->CreateEntity();
			reg->AddComponent(e, BenchPos{
				static_cast<float>(i),
				static_cast<float>(i) * 0.5f,
				static_cast<float>(i) * 0.25f
			}, q);
		}
		reg->FlushCommands(q);

		const auto view = reg->Pool<BenchPos>().ReadView();
		const BenchPos* data = view.data.data();
		const size_t n = view.data.size();

		for (auto _ : state)
		{
			float sumX = 0.0f;
			float sumY = 0.0f;
			float sumZ = 0.0f;
			for (size_t i = 0; i < n; ++i)
			{
				sumX += data[i].x;
				sumY += data[i].y;
				sumZ += data[i].z;
			}
			benchmark::DoNotOptimize(sumX);
			benchmark::DoNotOptimize(sumY);
			benchmark::DoNotOptimize(sumZ);
		}

		const int64_t totalItems = state.iterations() * static_cast<int64_t>(n);
		state.SetItemsProcessed(totalItems);
		state.SetBytesProcessed(totalItems * static_cast<int64_t>(sizeof(BenchPos)));
	}
	BENCHMARK(BM_ECS_SinglePool_LinearIteration)
		->Arg(10'000)
		->Arg(100'000)
		->Unit(benchmark::kNanosecond);

	void BM_ECS_TwoPool_Intersection_CachedView(benchmark::State& state)
	{
		constexpr uint32_t kCount = 10'000;
		auto reg = std::make_unique<Zero::EcsRegistry>();
		Zero::EcsCommandQueue q;

		for (uint32_t i = 0; i < kCount; ++i)
		{
			const Zero::Entity e = reg->CreateEntity();
			reg->AddComponent(e, BenchPos{ static_cast<float>(i), 1.0f, 2.0f }, q);
			if ((i & 1u) == 0u)
			{
				reg->AddComponent(e, BenchVel{ 0.1f, 0.2f, 0.3f }, q);
			}
		}
		reg->FlushCommands(q);

		Zero::CachedView<BenchPos, BenchVel> view(*reg);
		const auto& posPool = reg->Pool<BenchPos>();
		const auto& velPool = reg->Pool<BenchVel>();
		const uint32_t matched = view.Count();

		for (auto _ : state)
		{
			float accum = 0.0f;
			view.Each([&](uint32_t entityIdx) {
				const BenchPos* p = posPool.Get(entityIdx);
				const BenchVel* v = velPool.Get(entityIdx);
				accum += p->x * v->vx + p->y * v->vy + p->z * v->vz;
			});
			benchmark::DoNotOptimize(accum);
		}

		state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(matched));
	}
	BENCHMARK(BM_ECS_TwoPool_Intersection_CachedView)->Unit(benchmark::kNanosecond);

	void BM_ECS_TwoPool_Intersection_100Percent(benchmark::State& state)
	{
		constexpr uint32_t kCount = 10'000;
		auto reg = std::make_unique<Zero::EcsRegistry>();
		Zero::EcsCommandQueue q;

		for (uint32_t i = 0; i < kCount; ++i)
		{
			const Zero::Entity e = reg->CreateEntity();
			reg->AddComponent(e, BenchPos{ static_cast<float>(i), 1.0f, 2.0f }, q);
			reg->AddComponent(e, BenchVel{ 0.1f, 0.2f, 0.3f }, q);
		}
		reg->FlushCommands(q);

		Zero::CachedView<BenchPos, BenchVel> view(*reg);
		const auto& posPool = reg->Pool<BenchPos>();
		const auto& velPool = reg->Pool<BenchVel>();
		const uint32_t matched = view.Count();

		for (auto _ : state)
		{
			float accum = 0.0f;
			view.Each([&](uint32_t entityIdx) {
				const BenchPos* p = posPool.Get(entityIdx);
				const BenchVel* v = velPool.Get(entityIdx);
				accum += p->x * v->vx + p->y * v->vy + p->z * v->vz;
			});
			benchmark::DoNotOptimize(accum);
		}

		state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(matched));
	}
	BENCHMARK(BM_ECS_TwoPool_Intersection_100Percent)->Unit(benchmark::kNanosecond);

	// Dirty Epoch Tracking

	void BM_ECS_GetMutable_DirtyMarking(benchmark::State& state)
	{
		constexpr uint32_t kCount = 10'000;
		auto reg = std::make_unique<Zero::EcsRegistry>();
		Zero::EcsCommandQueue q;

		for (uint32_t i = 0; i < kCount; ++i)
		{
			const Zero::Entity e = reg->CreateEntity();
			reg->AddComponent(e, BenchPos{ static_cast<float>(i), 1.0f, 2.0f }, q);
		}
		reg->FlushCommands(q);

		auto& pool = reg->Pool<BenchPos>();
		const auto indicesSpan = pool.EntityIndices();
		const std::vector<uint32_t> entityIndices(indicesSpan.begin(), indicesSpan.end());
		const uint64_t epoch = reg->AdvanceEpoch();

		for (auto _ : state)
		{
			for (uint32_t idx : entityIndices)
			{
				auto ref = pool.GetMutable(idx, epoch, Zero::WriteSource::Gameplay);
				ref->x += 1.0f;
			}
			benchmark::DoNotOptimize(pool.data.data());
			benchmark::DoNotOptimize(pool.modifiedEpoch.data());
		}

		state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(kCount));
	}
	BENCHMARK(BM_ECS_GetMutable_DirtyMarking)->Unit(benchmark::kNanosecond);

	void BM_ECS_Get_DirectRead(benchmark::State& state)
	{
		constexpr uint32_t kCount = 10'000;
		auto reg = std::make_unique<Zero::EcsRegistry>();
		Zero::EcsCommandQueue q;

		for (uint32_t i = 0; i < kCount; ++i)
		{
			const Zero::Entity e = reg->CreateEntity();
			reg->AddComponent(e, BenchPos{ static_cast<float>(i), 1.0f, 2.0f }, q);
		}
		reg->FlushCommands(q);

		const auto& pool = reg->Pool<BenchPos>();
		const auto indicesSpan = pool.EntityIndices();
		const std::vector<uint32_t> entityIndices(indicesSpan.begin(), indicesSpan.end());

		for (auto _ : state)
		{
			float sum = 0.0f;
			for (uint32_t idx : entityIndices)
			{
				const BenchPos* p = pool.Get(idx);
				sum += p->x;
			}
			benchmark::DoNotOptimize(sum);
		}

		state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(kCount));
	}
	BENCHMARK(BM_ECS_Get_DirectRead)->Unit(benchmark::kNanosecond);

	// Stress Benchmarks

	std::unique_ptr<Zero::EcsRegistry>     g_enqueueReg;
	std::unique_ptr<Zero::EcsCommandQueue> g_enqueueQueue;
	Zero::Entity                           g_enqueueEntity = Zero::Invalid;

	void BM_ECS_Stress_MultiThreadEnqueue(benchmark::State& state)
	{
		if (state.thread_index() == 0)
		{
			g_enqueueReg    = std::make_unique<Zero::EcsRegistry>();
			g_enqueueQueue  = std::make_unique<Zero::EcsCommandQueue>();
			g_enqueueEntity = g_enqueueReg->CreateEntity();
			(void)g_enqueueReg->Pool<BenchPos>();
		}

		const BenchPos pos{ 1.0f, 2.0f, 3.0f };
		for (auto _ : state)
		{
			g_enqueueReg->AddComponent(g_enqueueEntity, pos, *g_enqueueQueue);
		}

		if (state.thread_index() == 0)
		{
			g_enqueueQueue.reset();
			g_enqueueReg.reset();
			g_enqueueEntity = Zero::Invalid;
		}

		state.SetItemsProcessed(state.iterations());
		state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(sizeof(BenchPos)));
	}
	BENCHMARK(BM_ECS_Stress_MultiThreadEnqueue)
		->Threads(2)
		->Threads(4)
		->Threads(8)
		->ThreadRange(1, 16)
		->Iterations(20'000)
		->Unit(benchmark::kNanosecond)
		->UseRealTime();

	void BM_ECS_Stress_LargeFlush100k(benchmark::State& state)
	{
		constexpr uint32_t kCount = 100'000;
		auto reg = std::make_unique<Zero::EcsRegistry>();
		Zero::EcsCommandQueue q;

		std::vector<Zero::Entity> entities;
		entities.reserve(kCount);
		for (uint32_t i = 0; i < kCount; ++i)
			entities.push_back(reg->CreateEntity());

		// Pre-warm pool capacity to 100k.
		for (uint32_t i = 0; i < kCount; ++i)
			reg->AddComponent(entities[i], BenchPos{ static_cast<float>(i), 1.0f, 2.0f }, q);
		reg->FlushCommands(q);
		for (uint32_t i = 0; i < kCount; ++i)
			reg->RemoveComponent<BenchPos>(entities[i], q);
		reg->FlushCommands(q);
		reg->ClearTombstones();

		bool populated = false;
		for (auto _ : state)
		{
			state.PauseTiming();
			if (populated)
			{
				for (uint32_t i = 0; i < kCount; ++i)
					reg->RemoveComponent<BenchPos>(entities[i], q);
				reg->FlushCommands(q);
				reg->ClearTombstones();
			}
			for (uint32_t i = 0; i < kCount; ++i)
				reg->AddComponent(entities[i], BenchPos{ static_cast<float>(i), 1.0f, 2.0f }, q);
			populated = true;
			state.ResumeTiming();

			uint64_t epoch = reg->FlushCommands(q);
			benchmark::DoNotOptimize(epoch);
		}

		state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(kCount));
	}
	BENCHMARK(BM_ECS_Stress_LargeFlush100k)->Unit(benchmark::kNanosecond);

	void BM_ECS_Stress_SlabOverflowFlush10k(benchmark::State& state)
	{
		constexpr uint32_t kCount = 10'000;
		auto reg = std::make_unique<Zero::EcsRegistry>();
		// 64 KiB slab holds ~512 128-byte aligned payloads; ~95% overflow to heap fallback.
		Zero::EcsCommandQueue q(64u * 1024u);

		std::vector<Zero::Entity> entities;
		entities.reserve(kCount);
		for (uint32_t i = 0; i < kCount; ++i)
			entities.push_back(reg->CreateEntity());

		const BenchLarge payload{ 0xCAFEBABEu, {} };
		for (uint32_t i = 0; i < kCount; ++i)
			reg->AddComponent(entities[i], payload, q);
		reg->FlushCommands(q);
		for (uint32_t i = 0; i < kCount; ++i)
			reg->RemoveComponent<BenchLarge>(entities[i], q);
		reg->FlushCommands(q);
		reg->ClearTombstones();

		bool populated = false;
		for (auto _ : state)
		{
			state.PauseTiming();
			if (populated)
			{
				for (uint32_t i = 0; i < kCount; ++i)
					reg->RemoveComponent<BenchLarge>(entities[i], q);
				reg->FlushCommands(q);
				reg->ClearTombstones();
			}
			for (uint32_t i = 0; i < kCount; ++i)
				reg->AddComponent(entities[i], payload, q);
			populated = true;
			state.ResumeTiming();

			uint64_t epoch = reg->FlushCommands(q);
			benchmark::DoNotOptimize(epoch);
		}

		state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(kCount));
		state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(kCount) * static_cast<int64_t>(sizeof(BenchLarge)));
	}
	BENCHMARK(BM_ECS_Stress_SlabOverflowFlush10k)->Unit(benchmark::kNanosecond);

	void BM_ECS_Stress_MixedChurnFrame_50k(benchmark::State& state)
	{
		constexpr uint32_t kLiveCount  = 50'000;
		constexpr uint32_t kChurnCount = 5'000;

		auto reg = std::make_unique<Zero::EcsRegistry>();
		Zero::EcsCommandQueue q;

		std::vector<Zero::Entity> entities;
		entities.reserve(kLiveCount);
		for (uint32_t i = 0; i < kLiveCount; ++i)
		{
			Zero::Entity e = reg->CreateEntity();
			entities.push_back(e);
			reg->AddComponent(e, BenchPos{ static_cast<float>(i), 1.0f, 2.0f }, q);
			reg->AddComponent(e, BenchVel{ 0.1f, 0.2f, 0.3f }, q);
			reg->AddComponent(e, BenchTag{ i }, q);
		}
		reg->FlushCommands(q);

		// Attach two CachedViews to include batched view invalidation & rebuild cost.
		auto viewPosVel = std::make_unique<Zero::CachedView<BenchPos, BenchVel>>(*reg);
		auto viewPosTag = std::make_unique<Zero::CachedView<BenchPos, BenchTag>>(*reg);

		uint32_t cycle = 0;
		for (auto _ : state)
		{
			state.PauseTiming();
			if (++cycle == 2000)
			{
				// Avoid 12-bit generation exhaustion on long runs.
				viewPosVel.reset();
				viewPosTag.reset();
				reg = std::make_unique<Zero::EcsRegistry>();
				entities.clear();
				for (uint32_t i = 0; i < kLiveCount; ++i)
				{
					Zero::Entity e = reg->CreateEntity();
					entities.push_back(e);
					reg->AddComponent(e, BenchPos{ static_cast<float>(i), 1.0f, 2.0f }, q);
					reg->AddComponent(e, BenchVel{ 0.1f, 0.2f, 0.3f }, q);
					reg->AddComponent(e, BenchTag{ i }, q);
				}
				reg->FlushCommands(q);
				viewPosVel = std::make_unique<Zero::CachedView<BenchPos, BenchVel>>(*reg);
				viewPosTag = std::make_unique<Zero::CachedView<BenchPos, BenchTag>>(*reg);
				cycle = 0;
			}

			// 1. Destroy 5,000 entities
			for (uint32_t i = 0; i < kChurnCount; ++i)
				reg->DestroyEntity(entities[i], q);
			// 2. Remove BenchTag from next 5,000
			for (uint32_t i = kChurnCount; i < kChurnCount * 2; ++i)
				reg->RemoveComponent<BenchTag>(entities[i], q);
			// 3. Upsert BenchPos on next 5,000
			for (uint32_t i = kChurnCount * 2; i < kChurnCount * 3; ++i)
				reg->SetComponent(entities[i], BenchPos{ static_cast<float>(cycle), 2.0f, 3.0f }, q);
			// 4. Create 5,000 replacement entities + components
			for (uint32_t i = 0; i < kChurnCount; ++i)
			{
				Zero::Entity e = reg->CreateEntity();
				entities[i] = e;
				reg->AddComponent(e, BenchPos{ static_cast<float>(i), 0.0f, 0.0f }, q);
				reg->AddComponent(e, BenchVel{ 0.5f, 0.5f, 0.5f }, q);
				reg->AddComponent(e, BenchTag{ cycle }, q);
			}
			// Restore BenchTag on slot [kChurnCount, kChurnCount*2) next frame by re-adding
			for (uint32_t i = kChurnCount; i < kChurnCount * 2; ++i)
				reg->AddComponent(entities[i], BenchTag{ cycle }, q);
			state.ResumeTiming();

			uint64_t epoch = reg->FlushCommands(q);
			reg->ClearTombstones();
			benchmark::DoNotOptimize(epoch);
		}

		uint32_t viewCount1 = viewPosVel->Count();
		uint32_t viewCount2 = viewPosTag->Count();
		benchmark::DoNotOptimize(viewCount1);
		benchmark::DoNotOptimize(viewCount2);
		state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(kChurnCount * 5));
	}
	BENCHMARK(BM_ECS_Stress_MixedChurnFrame_50k)->Unit(benchmark::kNanosecond);

	void BM_ECS_Stress_ThreePool_CachedView_100k(benchmark::State& state)
	{
		constexpr uint32_t kCount = 100'000;
		auto reg = std::make_unique<Zero::EcsRegistry>();
		Zero::EcsCommandQueue q;

		for (uint32_t i = 0; i < kCount; ++i)
		{
			const Zero::Entity e = reg->CreateEntity();
			reg->AddComponent(e, BenchPos{ static_cast<float>(i), 1.0f, 2.0f }, q);
			if ((i & 1u) == 0u)
				reg->AddComponent(e, BenchVel{ 0.1f, 0.2f, 0.3f }, q);
			if ((i % 10u) == 0u)
				reg->AddComponent(e, BenchTag{ i }, q);
		}
		reg->FlushCommands(q);

		Zero::CachedView<BenchPos, BenchVel, BenchTag> view(*reg);
		const auto& posPool = reg->Pool<BenchPos>();
		const auto& velPool = reg->Pool<BenchVel>();
		const auto& tagPool = reg->Pool<BenchTag>();
		const uint32_t matched = view.Count();

		for (auto _ : state)
		{
			float accum = 0.0f;
			view.Each([&](uint32_t entityIdx) {
				const BenchPos* p = posPool.Get(entityIdx);
				const BenchVel* v = velPool.Get(entityIdx);
				const BenchTag* t = tagPool.Get(entityIdx);
				accum += (p->x * v->vx + p->y * v->vy + p->z * v->vz) + static_cast<float>(t->flags & 1u);
			});
			benchmark::DoNotOptimize(accum);
		}

		state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(matched));
	}
	BENCHMARK(BM_ECS_Stress_ThreePool_CachedView_100k)->Unit(benchmark::kNanosecond);

	void BM_ECS_Stress_ThreePool_ComponentView_100k(benchmark::State& state)
	{
		constexpr uint32_t kCount = 100'000;
		auto reg = std::make_unique<Zero::EcsRegistry>();
		Zero::EcsCommandQueue q;

		for (uint32_t i = 0; i < kCount; ++i)
		{
			const Zero::Entity e = reg->CreateEntity();
			reg->AddComponent(e, BenchPos{ static_cast<float>(i), 1.0f, 2.0f }, q);
			if ((i & 1u) == 0u)
				reg->AddComponent(e, BenchVel{ 0.1f, 0.2f, 0.3f }, q);
			if ((i % 10u) == 0u)
				reg->AddComponent(e, BenchTag{ i }, q);
		}
		reg->FlushCommands(q);

		Zero::ComponentView<BenchPos, BenchVel, BenchTag> view(*reg);
		const uint32_t matched = view.Count();

		for (auto _ : state)
		{
			float accum = 0.0f;
			view.Each([&](Zero::Entity, const BenchPos& p, const BenchVel& v, const BenchTag& t) {
				accum += (p.x * v.vx + p.y * v.vy + p.z * v.vz) + static_cast<float>(t.flags & 1u);
			});
			benchmark::DoNotOptimize(accum);
		}

		state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(matched));
	}
	BENCHMARK(BM_ECS_Stress_ThreePool_ComponentView_100k)->Unit(benchmark::kNanosecond);
}
