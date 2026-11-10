#include <benchmark/benchmark.h>
#include <Engine/Math/Mat4.h>
#include <vector>
#include <immintrin.h>

namespace
{
	Zero::Mat4 MultiplyScalar(const Zero::Mat4& a, const Zero::Mat4& b) noexcept
	{
		Zero::Mat4 result;
		for (int col = 0; col < 4; ++col)
		{
			for (int row = 0; row < 4; ++row)
			{
				result.m[col * 4 + row] =
					a.m[0 * 4 + row] * b.m[col * 4 + 0] +
					a.m[1 * 4 + row] * b.m[col * 4 + 1] +
					a.m[2 * 4 + row] * b.m[col * 4 + 2] +
					a.m[3 * 4 + row] * b.m[col * 4 + 3];
			}
		}
		return result;
	}

	Zero::Mat4 MultiplySSE2(const Zero::Mat4& a, const Zero::Mat4& b) noexcept
	{
		Zero::Mat4 result;
		__m128 a0 = _mm_load_ps(&a.m[0]);
		__m128 a1 = _mm_load_ps(&a.m[4]);
		__m128 a2 = _mm_load_ps(&a.m[8]);
		__m128 a3 = _mm_load_ps(&a.m[12]);

		for (int col = 0; col < 4; ++col)
		{
			__m128 b0 = _mm_set1_ps(b.m[col * 4 + 0]);
			__m128 b1 = _mm_set1_ps(b.m[col * 4 + 1]);
			__m128 b2 = _mm_set1_ps(b.m[col * 4 + 2]);
			__m128 b3 = _mm_set1_ps(b.m[col * 4 + 3]);

			__m128 c = _mm_add_ps(
				_mm_add_ps(_mm_mul_ps(a0, b0), _mm_mul_ps(a1, b1)),
				_mm_add_ps(_mm_mul_ps(a2, b2), _mm_mul_ps(a3, b3))
			);
			_mm_store_ps(&result.m[col * 4], c);
		}
		return result;
	}

	__attribute__((target("avx2,fma")))
	Zero::Mat4 MultiplyAVX2FMA(const Zero::Mat4& a, const Zero::Mat4& b) noexcept
	{
		Zero::Mat4 result;
		__m128 a0 = _mm_load_ps(&a.m[0]);
		__m128 a1 = _mm_load_ps(&a.m[4]);
		__m128 a2 = _mm_load_ps(&a.m[8]);
		__m128 a3 = _mm_load_ps(&a.m[12]);

		for (int col = 0; col < 4; ++col)
		{
			__m128 b0 = _mm_set1_ps(b.m[col * 4 + 0]);
			__m128 b1 = _mm_set1_ps(b.m[col * 4 + 1]);
			__m128 b2 = _mm_set1_ps(b.m[col * 4 + 2]);
			__m128 b3 = _mm_set1_ps(b.m[col * 4 + 3]);

			__m128 c = _mm_fmadd_ps(a0, b0, _mm_fmadd_ps(a1, b1, _mm_fmadd_ps(a2, b2, _mm_mul_ps(a3, b3))));
			_mm_store_ps(&result.m[col * 4], c);
		}
		return result;
	}

	__attribute__((target("avx2,fma")))
	void MultiplyBatchAVX2FMA(const Zero::Mat4& transform, const Zero::Mat4* in, Zero::Mat4* out, size_t count) noexcept
	{
		__m256 a0 = _mm256_broadcast_ps((const __m128*)&transform.m[0]);
		__m256 a1 = _mm256_broadcast_ps((const __m128*)&transform.m[4]);
		__m256 a2 = _mm256_broadcast_ps((const __m128*)&transform.m[8]);
		__m256 a3 = _mm256_broadcast_ps((const __m128*)&transform.m[12]);

		size_t i = 0;
		for (; i + 2 <= count; i += 2)
		{
			const Zero::Mat4& m0 = in[i];
			const Zero::Mat4& m1 = in[i + 1];
			Zero::Mat4& out0 = out[i];
			Zero::Mat4& out1 = out[i + 1];

			for (int col = 0; col < 4; ++col)
			{
				__m256 b0 = _mm256_set_m128(_mm_set1_ps(m1.m[col * 4 + 0]), _mm_set1_ps(m0.m[col * 4 + 0]));
				__m256 b1 = _mm256_set_m128(_mm_set1_ps(m1.m[col * 4 + 1]), _mm_set1_ps(m0.m[col * 4 + 1]));
				__m256 b2 = _mm256_set_m128(_mm_set1_ps(m1.m[col * 4 + 2]), _mm_set1_ps(m0.m[col * 4 + 2]));
				__m256 b3 = _mm256_set_m128(_mm_set1_ps(m1.m[col * 4 + 3]), _mm_set1_ps(m0.m[col * 4 + 3]));

				__m256 c = _mm256_fmadd_ps(a0, b0, _mm256_fmadd_ps(a1, b1, _mm256_fmadd_ps(a2, b2, _mm256_mul_ps(a3, b3))));
				_mm_store_ps(&out0.m[col * 4], _mm256_castps256_ps128(c));
				_mm_store_ps(&out1.m[col * 4], _mm256_extractf128_ps(c, 1));
			}
		}
		for (; i < count; ++i)
		{
			out[i] = MultiplyAVX2FMA(transform, in[i]);
		}
	}

	// 1. Single Mat4 Multiplication
	void BM_Mat4_Mul_Scalar(benchmark::State& state)
	{
		Zero::Mat4 a = Zero::Mat4::Identity();
		Zero::Mat4 b = Zero::Mat4::Identity();
		a.m[3] = 2.0f; b.m[7] = 3.0f;

		for (auto _ : state)
		{
			Zero::Mat4 c = MultiplyScalar(a, b);
			benchmark::DoNotOptimize(c);
		}
	}
	BENCHMARK(BM_Mat4_Mul_Scalar);

	void BM_Mat4_Mul_SSE2(benchmark::State& state)
	{
		Zero::Mat4 a = Zero::Mat4::Identity();
		Zero::Mat4 b = Zero::Mat4::Identity();
		a.m[3] = 2.0f; b.m[7] = 3.0f;

		for (auto _ : state)
		{
			Zero::Mat4 c = MultiplySSE2(a, b);
			benchmark::DoNotOptimize(c);
		}
	}
	BENCHMARK(BM_Mat4_Mul_SSE2);

	__attribute__((target("avx2,fma")))
	void BM_Mat4_Mul_AVX2_FMA(benchmark::State& state)
	{
		Zero::Mat4 a = Zero::Mat4::Identity();
		Zero::Mat4 b = Zero::Mat4::Identity();
		a.m[3] = 2.0f; b.m[7] = 3.0f;

		for (auto _ : state)
		{
			Zero::Mat4 c = MultiplyAVX2FMA(a, b);
			benchmark::DoNotOptimize(c);
		}
	}
	BENCHMARK(BM_Mat4_Mul_AVX2_FMA);

	void BM_Mat4_Mul_Highway_Dynamic(benchmark::State& state)
	{
		Zero::Mat4 a = Zero::Mat4::Identity();
		Zero::Mat4 b = Zero::Mat4::Identity();
		a.m[3] = 2.0f; b.m[7] = 3.0f;

		for (auto _ : state)
		{
			Zero::Mat4 c = a * b;
			benchmark::DoNotOptimize(c);
		}
	}
	BENCHMARK(BM_Mat4_Mul_Highway_Dynamic);

	// 2. Batch Mat4 Multiplication (1,000 matrices)
	void BM_Mat4_Batch1k_Scalar(benchmark::State& state)
	{
		constexpr size_t N = 1000;
		std::vector<Zero::Mat4> in(N, Zero::Mat4::Identity());
		std::vector<Zero::Mat4> out(N);
		Zero::Mat4 transform = Zero::Mat4::Identity();
		transform.m[12] = 5.0f;

		for (auto _ : state)
		{
			for (size_t i = 0; i < N; ++i)
			{
				out[i] = MultiplyScalar(transform, in[i]);
			}
			benchmark::DoNotOptimize(out.data());
		}
		state.SetItemsProcessed(state.iterations() * N);
	}
	BENCHMARK(BM_Mat4_Batch1k_Scalar);

	void BM_Mat4_Batch1k_SSE2(benchmark::State& state)
	{
		constexpr size_t N = 1000;
		std::vector<Zero::Mat4> in(N, Zero::Mat4::Identity());
		std::vector<Zero::Mat4> out(N);
		Zero::Mat4 transform = Zero::Mat4::Identity();
		transform.m[12] = 5.0f;

		for (auto _ : state)
		{
			for (size_t i = 0; i < N; ++i)
			{
				out[i] = MultiplySSE2(transform, in[i]);
			}
			benchmark::DoNotOptimize(out.data());
		}
		state.SetItemsProcessed(state.iterations() * N);
	}
	BENCHMARK(BM_Mat4_Batch1k_SSE2);

	__attribute__((target("avx2,fma")))
	void BM_Mat4_Batch1k_AVX2_FMA(benchmark::State& state)
	{
		constexpr size_t N = 1000;
		std::vector<Zero::Mat4> in(N, Zero::Mat4::Identity());
		std::vector<Zero::Mat4> out(N);
		Zero::Mat4 transform = Zero::Mat4::Identity();
		transform.m[12] = 5.0f;

		for (auto _ : state)
		{
			MultiplyBatchAVX2FMA(transform, in.data(), out.data(), N);
			benchmark::DoNotOptimize(out.data());
		}
		state.SetItemsProcessed(state.iterations() * N);
	}
	BENCHMARK(BM_Mat4_Batch1k_AVX2_FMA);

	void BM_Mat4_Batch1k_Highway_Dynamic(benchmark::State& state)
	{
		constexpr size_t N = 1000;
		std::vector<Zero::Mat4> in(N, Zero::Mat4::Identity());
		std::vector<Zero::Mat4> out(N);
		Zero::Mat4 transform = Zero::Mat4::Identity();
		transform.m[12] = 5.0f;

		for (auto _ : state)
		{
			Zero::Mat4::MultiplyBatch(transform, in.data(), out.data(), N);
			benchmark::DoNotOptimize(out.data());
		}
		state.SetItemsProcessed(state.iterations() * N);
	}
	BENCHMARK(BM_Mat4_Batch1k_Highway_Dynamic);
}
