#include <benchmark/benchmark.h>
#include <Engine/Math/Vec3.h>
#include <Engine/Math/Mat4.h>
#include <vector>
#include <cmath>
#include <hwy/highway.h>

namespace
{
	namespace hn = hwy::HWY_NAMESPACE;

	// 1. Single Vec3 Dot Product
	void BM_Vec3_SingleDot_Scalar(benchmark::State& state)
	{
		Zero::Vec3 a{ 1.0f, 2.0f, 3.0f };
		Zero::Vec3 b{ 4.0f, 5.0f, 6.0f };

		for (auto _ : state)
		{
			float d = Zero::Vec3::Dot(a, b);
			benchmark::DoNotOptimize(d);
		}
	}
	BENCHMARK(BM_Vec3_SingleDot_Scalar);

	void BM_Vec3_SingleDot_Highway(benchmark::State& state)
	{
		Zero::Vec3 a{ 1.0f, 2.0f, 3.0f };
		Zero::Vec3 b{ 4.0f, 5.0f, 6.0f };
		const hn::FixedTag<float, 4> d;

		for (auto _ : state)
		{
			// Loading 3 floats into vector + horizontal reduction
			alignas(16) float aBuf[4] = { a.x, a.y, a.z, 0.0f };
			alignas(16) float bBuf[4] = { b.x, b.y, b.z, 0.0f };
			auto va = hn::Load(d, aBuf);
			auto vb = hn::Load(d, bBuf);
			auto prod = hn::Mul(va, vb);
			float dot = hn::ReduceSum(d, prod);
			benchmark::DoNotOptimize(dot);
		}
	}
	BENCHMARK(BM_Vec3_SingleDot_Highway);

	// 2. Batch Vec3 Dot Products (10,000 vectors in SoA layout)
	void BM_Vec3_Batch10k_Dot_Scalar(benchmark::State& state)
	{
		constexpr size_t N = 10000;
		std::vector<float> ax(N, 1.0f), ay(N, 2.0f), az(N, 3.0f);
		std::vector<float> bx(N, 4.0f), by(N, 5.0f), bz(N, 6.0f);
		std::vector<float> out(N, 0.0f);

		for (auto _ : state)
		{
			for (size_t i = 0; i < N; ++i)
			{
				out[i] = ax[i] * bx[i] + ay[i] * by[i] + az[i] * bz[i];
			}
			benchmark::DoNotOptimize(out.data());
		}
		state.SetItemsProcessed(state.iterations() * N);
	}
	BENCHMARK(BM_Vec3_Batch10k_Dot_Scalar);

	void BM_Vec3_Batch10k_Dot_Highway(benchmark::State& state)
	{
		constexpr size_t N = 10000;
		std::vector<float> ax(N, 1.0f), ay(N, 2.0f), az(N, 3.0f);
		std::vector<float> bx(N, 4.0f), by(N, 5.0f), bz(N, 6.0f);
		std::vector<float> out(N, 0.0f);

		const hn::ScalableTag<float> d;
		const size_t lanes = hn::Lanes(d);

		for (auto _ : state)
		{
			for (size_t i = 0; i < N; i += lanes)
			{
				auto vax = hn::LoadU(d, &ax[i]);
				auto vay = hn::LoadU(d, &ay[i]);
				auto vaz = hn::LoadU(d, &az[i]);

				auto vbx = hn::LoadU(d, &bx[i]);
				auto vby = hn::LoadU(d, &by[i]);
				auto vbz = hn::LoadU(d, &bz[i]);

				auto dot = hn::MulAdd(vax, vbx, hn::MulAdd(vay, vby, hn::Mul(vaz, vbz)));
				hn::StoreU(dot, d, &out[i]);
			}
			benchmark::DoNotOptimize(out.data());
		}
		state.SetItemsProcessed(state.iterations() * N);
	}
	BENCHMARK(BM_Vec3_Batch10k_Dot_Highway);

	// 3. Batch Mat4 * Vec3 Transform (10,000 points in SoA)
	void BM_Vec3_Transform10k_Scalar(benchmark::State& state)
	{
		constexpr size_t N = 10000;
		std::vector<float> x(N, 1.0f), y(N, 2.0f), z(N, 3.0f);
		std::vector<float> rx(N, 0.0f), ry(N, 0.0f), rz(N, 0.0f);

		Zero::Mat4 m = Zero::Mat4::Identity();
		m.m[12] = 10.0f; m.m[13] = 20.0f; m.m[14] = 30.0f;

		for (auto _ : state)
		{
			for (size_t i = 0; i < N; ++i)
			{
				rx[i] = m.m[0] * x[i] + m.m[4] * y[i] + m.m[8]  * z[i] + m.m[12];
				ry[i] = m.m[1] * x[i] + m.m[5] * y[i] + m.m[9]  * z[i] + m.m[13];
				rz[i] = m.m[2] * x[i] + m.m[6] * y[i] + m.m[10] * z[i] + m.m[14];
			}
			benchmark::DoNotOptimize(rx.data());
		}
		state.SetItemsProcessed(state.iterations() * N);
	}
	BENCHMARK(BM_Vec3_Transform10k_Scalar);

	void BM_Vec3_Transform10k_Highway(benchmark::State& state)
	{
		constexpr size_t N = 10000;
		std::vector<float> x(N, 1.0f), y(N, 2.0f), z(N, 3.0f);
		std::vector<float> rx(N, 0.0f), ry(N, 0.0f), rz(N, 0.0f);

		Zero::Mat4 m = Zero::Mat4::Identity();
		m.m[12] = 10.0f; m.m[13] = 20.0f; m.m[14] = 30.0f;

		const hn::ScalableTag<float> d;
		const size_t lanes = hn::Lanes(d);

		const auto m00 = hn::Set(d, m.m[0]);  const auto m01 = hn::Set(d, m.m[4]);
		const auto m02 = hn::Set(d, m.m[8]);  const auto m03 = hn::Set(d, m.m[12]);

		const auto m10 = hn::Set(d, m.m[1]);  const auto m11 = hn::Set(d, m.m[5]);
		const auto m12 = hn::Set(d, m.m[9]);  const auto m13 = hn::Set(d, m.m[13]);

		const auto m20 = hn::Set(d, m.m[2]);  const auto m21 = hn::Set(d, m.m[6]);
		const auto m22 = hn::Set(d, m.m[10]); const auto m23 = hn::Set(d, m.m[14]);

		for (auto _ : state)
		{
			for (size_t i = 0; i < N; i += lanes)
			{
				auto px = hn::LoadU(d, &x[i]);
				auto py = hn::LoadU(d, &y[i]);
				auto pz = hn::LoadU(d, &z[i]);

				auto resX = hn::MulAdd(m00, px, hn::MulAdd(m01, py, hn::MulAdd(m02, pz, m03)));
				auto resY = hn::MulAdd(m10, px, hn::MulAdd(m11, py, hn::MulAdd(m12, pz, m13)));
				auto resZ = hn::MulAdd(m20, px, hn::MulAdd(m21, py, hn::MulAdd(m22, pz, m23)));

				hn::StoreU(resX, d, &rx[i]);
				hn::StoreU(resY, d, &ry[i]);
				hn::StoreU(resZ, d, &rz[i]);
			}
			benchmark::DoNotOptimize(rx.data());
		}
		state.SetItemsProcessed(state.iterations() * N);
	}
	BENCHMARK(BM_Vec3_Transform10k_Highway);
}

