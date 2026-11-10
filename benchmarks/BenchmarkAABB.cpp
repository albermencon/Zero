#include <benchmark/benchmark.h>
#include <Engine/Math/AABB.h>
#include <Engine/Math/Vec3.h>
#include <vector>
#include <hwy/highway.h>

namespace
{
	namespace hn = hwy::HWY_NAMESPACE;

	bool IntersectScalar(const Zero::AABB& a, const Zero::AABB& b) noexcept
	{
		return (a.min.x <= b.max.x && a.max.x >= b.min.x) &&
		       (a.min.y <= b.max.y && a.max.y >= b.min.y) &&
		       (a.min.z <= b.max.z && a.max.z >= b.min.z);
	}

	// 1. Single AABB Intersection
	void BM_AABB_SingleIntersect_Scalar(benchmark::State& state)
	{
		Zero::AABB a{ { -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f } };
		Zero::AABB b{ { 0.5f, 0.5f, 0.5f }, { 2.0f, 2.0f, 2.0f } };

		for (auto _ : state)
		{
			bool hit = IntersectScalar(a, b);
			benchmark::DoNotOptimize(hit);
		}
	}
	BENCHMARK(BM_AABB_SingleIntersect_Scalar);

	// 2. Batch AABB vs AABB Overlap (10,000 AABBs in SoA layout vs Query Box)
	void BM_AABB_Batch10k_Scalar(benchmark::State& state)
	{
		constexpr size_t N = 10000;
		std::vector<float> minX(N, -1.0f), minY(N, -1.0f), minZ(N, -1.0f);
		std::vector<float> maxX(N, 1.0f), maxY(N, 1.0f), maxZ(N, 1.0f);
		std::vector<uint32_t> hits(N, 0);

		const float qMinX = 0.0f, qMinY = 0.0f, qMinZ = 0.0f;
		const float qMaxX = 2.0f, qMaxY = 2.0f, qMaxZ = 2.0f;

		for (auto _ : state)
		{
			uint32_t hitCount = 0;
			for (size_t i = 0; i < N; ++i)
			{
				if (minX[i] <= qMaxX && maxX[i] >= qMinX &&
				    minY[i] <= qMaxY && maxY[i] >= qMinY &&
				    minZ[i] <= qMaxZ && maxZ[i] >= qMinZ)
				{
					hits[hitCount++] = static_cast<uint32_t>(i);
				}
			}
			benchmark::DoNotOptimize(hitCount);
			benchmark::DoNotOptimize(hits.data());
		}
		state.SetItemsProcessed(state.iterations() * N);
	}
	BENCHMARK(BM_AABB_Batch10k_Scalar);

	void BM_AABB_Batch10k_Highway(benchmark::State& state)
	{
		constexpr size_t N = 10000;
		std::vector<float> minX(N, -1.0f), minY(N, -1.0f), minZ(N, -1.0f);
		std::vector<float> maxX(N, 1.0f), maxY(N, 1.0f), maxZ(N, 1.0f);
		std::vector<uint32_t> hits(N, 0);

		const float qMinX = 0.0f, qMinY = 0.0f, qMinZ = 0.0f;
		const float qMaxX = 2.0f, qMaxY = 2.0f, qMaxZ = 2.0f;

		const hn::ScalableTag<float> d;
		const hn::ScalableTag<uint32_t> du;
		const size_t lanes = hn::Lanes(d);

		const auto vQMinX = hn::Set(d, qMinX); const auto vQMaxX = hn::Set(d, qMaxX);
		const auto vQMinY = hn::Set(d, qMinY); const auto vQMaxY = hn::Set(d, qMaxY);
		const auto vQMinZ = hn::Set(d, qMinZ); const auto vQMaxZ = hn::Set(d, qMaxZ);

		for (auto _ : state)
		{
			uint32_t hitCount = 0;
			for (size_t i = 0; i < N; i += lanes)
			{
				auto bMinX = hn::LoadU(d, &minX[i]);
				auto bMaxX = hn::LoadU(d, &maxX[i]);
				auto bMinY = hn::LoadU(d, &minY[i]);
				auto bMaxY = hn::LoadU(d, &maxY[i]);
				auto bMinZ = hn::LoadU(d, &minZ[i]);
				auto bMaxZ = hn::LoadU(d, &maxZ[i]);

				auto overlapX = hn::And(hn::Le(bMinX, vQMaxX), hn::Ge(bMaxX, vQMinX));
				auto overlapY = hn::And(hn::Le(bMinY, vQMaxY), hn::Ge(bMaxY, vQMinY));
				auto overlapZ = hn::And(hn::Le(bMinZ, vQMaxZ), hn::Ge(bMaxZ, vQMinZ));

				auto hit = hn::And(overlapX, hn::And(overlapY, overlapZ));
				if (hn::AllFalse(d, hit))
					continue;

				auto indices = hn::Iota(du, static_cast<uint32_t>(i));
				hn::CompressStore(indices, hn::RebindMask(du, hit), du, hits.data() + hitCount);
				hitCount += static_cast<uint32_t>(hn::CountTrue(d, hit));
			}
			benchmark::DoNotOptimize(hitCount);
			benchmark::DoNotOptimize(hits.data());
		}
		state.SetItemsProcessed(state.iterations() * N);
	}
	BENCHMARK(BM_AABB_Batch10k_Highway);
}

