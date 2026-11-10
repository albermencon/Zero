#include <benchmark/benchmark.h>
#include <Engine/Math/Mat4.h>
#include <Engine/Math/Frustum.h>
#include <vector>
#include <immintrin.h>

namespace
{
	Zero::Frustum SetupBenchmarkFrustum()
	{
		Zero::Mat4 proj{};
		const float aspect = 1.777f;
		const float tanHalfFov = 0.577f;
		const float zNear = 0.5f;
		const float zFar = 1000.0f;

		proj.m[0] = 1.0f / (aspect * tanHalfFov);
		proj.m[5] = -1.0f / tanHalfFov;
		proj.m[10] = -zFar / (zFar - zNear);
		proj.m[11] = -1.0f;
		proj.m[14] = -(zFar * zNear) / (zFar - zNear);

		Zero::Mat4 view = Zero::Mat4::Identity();
		return Zero::Frustum::FromViewProj(proj * view);
	}

	struct AABBDataset
	{
		std::vector<float> minX, minY, minZ;
		std::vector<float> maxX, maxY, maxZ;
		std::vector<uint32_t> visibleIndices;

		explicit AABBDataset(uint32_t count)
			: minX(count), minY(count), minZ(count)
			, maxX(count), maxY(count), maxZ(count)
			, visibleIndices(count)
		{
			for (uint32_t i = 0; i < count; ++i)
			{
				float x = static_cast<float>((static_cast<int>(i % 64) - 32) * 15);
				float y = static_cast<float>((static_cast<int>((i / 64) % 64) - 32) * 15);
				float z = -static_cast<float>((i % 128) * 8);

				minX[i] = x - 5.0f;
				maxX[i] = x + 5.0f;
				minY[i] = y - 5.0f;
				maxY[i] = y + 5.0f;
				minZ[i] = z - 5.0f;
				maxZ[i] = z + 5.0f;
			}
		}
	};

	// 1. Scalar Baseline
	void BM_FrustumCull_Scalar_10k(benchmark::State& state)
	{
		Zero::Frustum frustum = SetupBenchmarkFrustum();
		constexpr uint32_t kCount = 10000;
		AABBDataset data(kCount);

		for (auto _ : state)
		{
			uint32_t visible = 0;
			for (uint32_t i = 0; i < kCount; ++i)
			{
				if (frustum.TestAABB(data.minX[i], data.minY[i], data.minZ[i],
				                     data.maxX[i], data.maxY[i], data.maxZ[i]))
				{
					data.visibleIndices[visible++] = i;
				}
			}
			benchmark::DoNotOptimize(visible);
			benchmark::DoNotOptimize(data.visibleIndices.data());
		}
		state.SetItemsProcessed(state.iterations() * kCount);
	}
	BENCHMARK(BM_FrustumCull_Scalar_10k);

	// 2. Explicit SSE2 (128-bit)
	void BM_FrustumCull_SSE2_10k(benchmark::State& state)
	{
		Zero::Frustum frustum = SetupBenchmarkFrustum();
		constexpr uint32_t kCount = 10000;
		AABBDataset data(kCount);

		const __m128 vMargin = _mm_setzero_ps();
		const __m128 zero = _mm_setzero_ps();

		for (auto _ : state)
		{
			uint32_t visibleCount = 0;
			uint32_t i = 0;

			for (; i + 4 <= kCount; i += 4)
			{
				__m128 bMinX = _mm_loadu_ps(data.minX.data() + i);
				__m128 bMaxX = _mm_loadu_ps(data.maxX.data() + i);
				__m128 bMinY = _mm_loadu_ps(data.minY.data() + i);
				__m128 bMaxY = _mm_loadu_ps(data.maxY.data() + i);
				__m128 bMinZ = _mm_loadu_ps(data.minZ.data() + i);
				__m128 bMaxZ = _mm_loadu_ps(data.maxZ.data() + i);

				__m128 culledMask = zero;

				for (int p = 0; p < 6; ++p)
				{
					const auto& pl = frustum.planes[p];
					__m128 vA = _mm_set1_ps(pl.a);
					__m128 vB = _mm_set1_ps(pl.b);
					__m128 vC = _mm_set1_ps(pl.c);
					__m128 vD = _mm_set1_ps(pl.d);

					__m128 px = (pl.a > 0.0f) ? bMaxX : bMinX;
					__m128 py = (pl.b > 0.0f) ? bMaxY : bMinY;
					__m128 pz = (pl.c > 0.0f) ? bMaxZ : bMinZ;

					__m128 dist = _mm_add_ps(
						_mm_add_ps(_mm_mul_ps(vA, px), _mm_mul_ps(vB, py)),
						_mm_add_ps(_mm_mul_ps(vC, pz), vD)
					);

					__m128 outside = _mm_cmplt_ps(dist, vMargin);
					culledMask = _mm_or_ps(culledMask, outside);
				}

				int visibleMask = (~_mm_movemask_ps(culledMask)) & 0xF;
				if (visibleMask == 0xF)
				{
					data.visibleIndices[visibleCount++] = i + 0;
					data.visibleIndices[visibleCount++] = i + 1;
					data.visibleIndices[visibleCount++] = i + 2;
					data.visibleIndices[visibleCount++] = i + 3;
				}
				else if (visibleMask != 0)
				{
					if (visibleMask & 0x1) data.visibleIndices[visibleCount++] = i + 0;
					if (visibleMask & 0x2) data.visibleIndices[visibleCount++] = i + 1;
					if (visibleMask & 0x4) data.visibleIndices[visibleCount++] = i + 2;
					if (visibleMask & 0x8) data.visibleIndices[visibleCount++] = i + 3;
				}
			}

			for (; i < kCount; ++i)
			{
				if (frustum.TestAABB(data.minX[i], data.minY[i], data.minZ[i],
				                     data.maxX[i], data.maxY[i], data.maxZ[i]))
				{
					data.visibleIndices[visibleCount++] = i;
				}
			}

			benchmark::DoNotOptimize(visibleCount);
			benchmark::DoNotOptimize(data.visibleIndices.data());
		}
		state.SetItemsProcessed(state.iterations() * kCount);
	}
	BENCHMARK(BM_FrustumCull_SSE2_10k);

	// 3. Explicit AVX2 + FMA3 (256-bit)
	__attribute__((target("avx2,fma")))
	void BM_FrustumCull_AVX2_FMA_10k(benchmark::State& state)
	{
		Zero::Frustum frustum = SetupBenchmarkFrustum();
		constexpr uint32_t kCount = 10000;
		AABBDataset data(kCount);

		const __m256 vMargin = _mm256_setzero_ps();
		const __m256 zero = _mm256_setzero_ps();

		for (auto _ : state)
		{
			uint32_t visibleCount = 0;
			uint32_t i = 0;

			for (; i + 8 <= kCount; i += 8)
			{
				__m256 bMinX = _mm256_loadu_ps(data.minX.data() + i);
				__m256 bMaxX = _mm256_loadu_ps(data.maxX.data() + i);
				__m256 bMinY = _mm256_loadu_ps(data.minY.data() + i);
				__m256 bMaxY = _mm256_loadu_ps(data.maxY.data() + i);
				__m256 bMinZ = _mm256_loadu_ps(data.minZ.data() + i);
				__m256 bMaxZ = _mm256_loadu_ps(data.maxZ.data() + i);

				__m256 culledMask = zero;

				for (int p = 0; p < 6; ++p)
				{
					const auto& pl = frustum.planes[p];
					__m256 vA = _mm256_set1_ps(pl.a);
					__m256 vB = _mm256_set1_ps(pl.b);
					__m256 vC = _mm256_set1_ps(pl.c);
					__m256 vD = _mm256_set1_ps(pl.d);

					__m256 px = (pl.a > 0.0f) ? bMaxX : bMinX;
					__m256 py = (pl.b > 0.0f) ? bMaxY : bMinY;
					__m256 pz = (pl.c > 0.0f) ? bMaxZ : bMinZ;

					__m256 dist = _mm256_fmadd_ps(vA, px, _mm256_fmadd_ps(vB, py, _mm256_fmadd_ps(vC, pz, vD)));
					__m256 outside = _mm256_cmp_ps(dist, vMargin, _CMP_LT_OQ);
					culledMask = _mm256_or_ps(culledMask, outside);
				}

				int visibleMask = (~_mm256_movemask_ps(culledMask)) & 0xFF;
				while (visibleMask)
				{
					int bit = _tzcnt_u32(visibleMask);
					data.visibleIndices[visibleCount++] = i + bit;
					visibleMask &= visibleMask - 1;
				}
			}

			for (; i < kCount; ++i)
			{
				if (frustum.TestAABB(data.minX[i], data.minY[i], data.minZ[i],
				                     data.maxX[i], data.maxY[i], data.maxZ[i]))
				{
					data.visibleIndices[visibleCount++] = i;
				}
			}

			benchmark::DoNotOptimize(visibleCount);
			benchmark::DoNotOptimize(data.visibleIndices.data());
		}
		state.SetItemsProcessed(state.iterations() * kCount);
	}
	BENCHMARK(BM_FrustumCull_AVX2_FMA_10k);

	// 4. Google Highway Dynamic Dispatch
	void BM_FrustumCull_Highway_Dynamic_10k(benchmark::State& state)
	{
		Zero::Frustum frustum = SetupBenchmarkFrustum();
		constexpr uint32_t kCount = 10000;
		AABBDataset data(kCount);

		for (auto _ : state)
		{
			uint32_t visible = frustum.CullAABBBatch(
				data.minX.data(), data.minY.data(), data.minZ.data(),
				data.maxX.data(), data.maxY.data(), data.maxZ.data(),
				kCount,
				data.visibleIndices.data()
			);
			benchmark::DoNotOptimize(visible);
			benchmark::DoNotOptimize(data.visibleIndices.data());
		}
		state.SetItemsProcessed(state.iterations() * kCount);
	}
	BENCHMARK(BM_FrustumCull_Highway_Dynamic_10k);
}
