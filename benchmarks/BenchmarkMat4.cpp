#include <benchmark/benchmark.h>
#include <Engine/Math/Mat4.h>
#include <Engine/Math/Vec3.h>
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

	void TransformPointsScalar(const Zero::Mat4& m, const Zero::Vec3* in, Zero::Vec3* out, size_t count) noexcept
	{
		const float m0 = m.m[0], m4 = m.m[4], m8  = m.m[8],  m12 = m.m[12];
		const float m1 = m.m[1], m5 = m.m[5], m9  = m.m[9],  m13 = m.m[13];
		const float m2 = m.m[2], m6 = m.m[6], m10 = m.m[10], m14 = m.m[14];

		for (size_t i = 0; i < count; ++i)
		{
			const float x = in[i].x;
			const float y = in[i].y;
			const float z = in[i].z;

			out[i].x = m0 * x + m4 * y + m8  * z + m12;
			out[i].y = m1 * x + m5 * y + m9  * z + m13;
			out[i].z = m2 * x + m6 * y + m10 * z + m14;
		}
	}

	__attribute__((target("avx2,fma")))
	void TransformPointsAVX2_AoS(const Zero::Mat4& m, const Zero::Vec3* in, Zero::Vec3* out, size_t count) noexcept
	{
		const __m128 m0 = _mm_set1_ps(m.m[0]);
		const __m128 m4 = _mm_set1_ps(m.m[4]);
		const __m128 m8 = _mm_set1_ps(m.m[8]);
		const __m128 m12 = _mm_set1_ps(m.m[12]);

		const __m128 m1 = _mm_set1_ps(m.m[1]);
		const __m128 m5 = _mm_set1_ps(m.m[5]);
		const __m128 m9 = _mm_set1_ps(m.m[9]);
		const __m128 m13 = _mm_set1_ps(m.m[13]);

		const __m128 m2 = _mm_set1_ps(m.m[2]);
		const __m128 m6 = _mm_set1_ps(m.m[6]);
		const __m128 m10 = _mm_set1_ps(m.m[10]);
		const __m128 m14 = _mm_set1_ps(m.m[14]);

		size_t i = 0;
		for (; i + 4 <= count; i += 4)
		{
			__m128 x = _mm_set_ps(in[i + 3].x, in[i + 2].x, in[i + 1].x, in[i].x);
			__m128 y = _mm_set_ps(in[i + 3].y, in[i + 2].y, in[i + 1].y, in[i].y);
			__m128 z = _mm_set_ps(in[i + 3].z, in[i + 2].z, in[i + 1].z, in[i].z);

			__m128 rx = _mm_fmadd_ps(m0, x, _mm_fmadd_ps(m4, y, _mm_fmadd_ps(m8, z, m12)));
			__m128 ry = _mm_fmadd_ps(m1, x, _mm_fmadd_ps(m5, y, _mm_fmadd_ps(m9, z, m13)));
			__m128 rz = _mm_fmadd_ps(m2, x, _mm_fmadd_ps(m6, y, _mm_fmadd_ps(m10, z, m14)));

			alignas(16) float ox[4];
			alignas(16) float oy[4];
			alignas(16) float oz[4];
			_mm_store_ps(ox, rx);
			_mm_store_ps(oy, ry);
			_mm_store_ps(oz, rz);

			for (int j = 0; j < 4; ++j)
			{
				out[i + j].x = ox[j];
				out[i + j].y = oy[j];
				out[i + j].z = oz[j];
			}
		}

		for (; i < count; ++i)
		{
			const float x = in[i].x;
			const float y = in[i].y;
			const float z = in[i].z;

			out[i].x = m.m[0] * x + m.m[4] * y + m.m[8]  * z + m.m[12];
			out[i].y = m.m[1] * x + m.m[5] * y + m.m[9]  * z + m.m[13];
			out[i].z = m.m[2] * x + m.m[6] * y + m.m[10] * z + m.m[14];
		}
	}

	void BM_TransformPoints_10k_Scalar(benchmark::State& state)
	{
		constexpr size_t N = 10000;
		std::vector<Zero::Vec3> in(N, Zero::Vec3(1.0f, 2.0f, 3.0f));
		std::vector<Zero::Vec3> out(N);
		Zero::Mat4 transform = Zero::Mat4::Translate(Zero::Vec3(10.0f, 20.0f, 30.0f));

		for (auto _ : state)
		{
			TransformPointsScalar(transform, in.data(), out.data(), N);
			benchmark::DoNotOptimize(out.data());
		}
		state.SetItemsProcessed(state.iterations() * N);
	}
	BENCHMARK(BM_TransformPoints_10k_Scalar);

	__attribute__((target("avx2,fma")))
	void BM_TransformPoints_10k_AVX2(benchmark::State& state)
	{
		constexpr size_t N = 10000;
		std::vector<Zero::Vec3> in(N, Zero::Vec3(1.0f, 2.0f, 3.0f));
		std::vector<Zero::Vec3> out(N);
		Zero::Mat4 transform = Zero::Mat4::Translate(Zero::Vec3(10.0f, 20.0f, 30.0f));

		for (auto _ : state)
		{
			TransformPointsAVX2_AoS(transform, in.data(), out.data(), N);
			benchmark::DoNotOptimize(out.data());
		}
		state.SetItemsProcessed(state.iterations() * N);
	}
	void BM_TransformPoints_10k_Highway_Dynamic(benchmark::State& state)
	{
		constexpr size_t N = 10000;
		std::vector<Zero::Vec3> in(N, Zero::Vec3(1.0f, 2.0f, 3.0f));
		std::vector<Zero::Vec3> out(N);
		Zero::Mat4 transform = Zero::Mat4::Translate(Zero::Vec3(10.0f, 20.0f, 30.0f));

		for (auto _ : state)
		{
			Zero::Mat4::TransformPointsBatch(transform, in.data(), out.data(), N);
			benchmark::DoNotOptimize(out.data());
		}
		state.SetItemsProcessed(state.iterations() * N);
	}
	BENCHMARK(BM_TransformPoints_10k_Highway_Dynamic);
}
