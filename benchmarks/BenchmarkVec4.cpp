#include <benchmark/benchmark.h>
#include <Engine/Math/Vec4.h>
#include <vector>
#include <cmath>
#include <hwy/highway.h>

namespace
{
	namespace hn = hwy::HWY_NAMESPACE;

	// 1. Single Vec4 Addition
	void BM_Vec4_SingleAdd_Scalar(benchmark::State& state)
	{
		Zero::Vec4 a{ 1.0f, 2.0f, 3.0f, 4.0f };
		Zero::Vec4 b{ 5.0f, 6.0f, 7.0f, 8.0f };

		for (auto _ : state)
		{
			Zero::Vec4 c = a + b;
			benchmark::DoNotOptimize(c);
		}
	}
	BENCHMARK(BM_Vec4_SingleAdd_Scalar);

	void BM_Vec4_SingleAdd_Highway(benchmark::State& state)
	{
		Zero::Vec4 a{ 1.0f, 2.0f, 3.0f, 4.0f };
		Zero::Vec4 b{ 5.0f, 6.0f, 7.0f, 8.0f };
		const hn::FixedTag<float, 4> d;

		for (auto _ : state)
		{
			auto va = hn::LoadU(d, &a.x);
			auto vb = hn::LoadU(d, &b.x);
			auto vc = hn::Add(va, vb);
			Zero::Vec4 c;
			hn::StoreU(vc, d, &c.x);
			benchmark::DoNotOptimize(c);
		}
	}
	BENCHMARK(BM_Vec4_SingleAdd_Highway);

	// 2. Batch Vec4 Dot Products (10,000 vectors in SoA)
	void BM_Vec4_Batch10k_Dot_Scalar(benchmark::State& state)
	{
		constexpr size_t N = 10000;
		std::vector<float> ax(N, 1.0f), ay(N, 2.0f), az(N, 3.0f), aw(N, 4.0f);
		std::vector<float> bx(N, 5.0f), by(N, 6.0f), bz(N, 7.0f), bw(N, 8.0f);
		std::vector<float> out(N, 0.0f);

		for (auto _ : state)
		{
			for (size_t i = 0; i < N; ++i)
			{
				out[i] = ax[i] * bx[i] + ay[i] * by[i] + az[i] * bz[i] + aw[i] * bw[i];
			}
			benchmark::DoNotOptimize(out.data());
		}
		state.SetItemsProcessed(state.iterations() * N);
	}
	BENCHMARK(BM_Vec4_Batch10k_Dot_Scalar);

	void BM_Vec4_Batch10k_Dot_Highway(benchmark::State& state)
	{
		constexpr size_t N = 10000;
		std::vector<float> ax(N, 1.0f), ay(N, 2.0f), az(N, 3.0f), aw(N, 4.0f);
		std::vector<float> bx(N, 5.0f), by(N, 6.0f), bz(N, 7.0f), bw(N, 8.0f);
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
				auto vaw = hn::LoadU(d, &aw[i]);

				auto vbx = hn::LoadU(d, &bx[i]);
				auto vby = hn::LoadU(d, &by[i]);
				auto vbz = hn::LoadU(d, &bz[i]);
				auto vbw = hn::LoadU(d, &bw[i]);

				auto dot = hn::MulAdd(vax, vbx, hn::MulAdd(vay, vby, hn::MulAdd(vaz, vbz, hn::Mul(vaw, vbw))));
				hn::StoreU(dot, d, &out[i]);
			}
			benchmark::DoNotOptimize(out.data());
		}
		state.SetItemsProcessed(state.iterations() * N);
	}
	BENCHMARK(BM_Vec4_Batch10k_Dot_Highway);
}

