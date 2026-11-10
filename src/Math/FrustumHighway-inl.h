#if defined(ZERO_SRC_MATH_FRUSTUM_HIGHWAY_INL_H_) == defined(HWY_TARGET_TOGGLE)
#ifdef ZERO_SRC_MATH_FRUSTUM_HIGHWAY_INL_H_
#undef ZERO_SRC_MATH_FRUSTUM_HIGHWAY_INL_H_
#else
#define ZERO_SRC_MATH_FRUSTUM_HIGHWAY_INL_H_
#endif

#include <hwy/highway.h>
#include <Engine/Math/Frustum.h>

HWY_BEFORE_NAMESPACE();
namespace Zero::HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

uint32_t CullAABBBatchHighway(
	const Frustum& frustum,
	const float* minX, const float* minY, const float* minZ,
	const float* maxX, const float* maxY, const float* maxZ,
	uint32_t count,
	uint32_t* outVisibleIndices,
	float margin)
{
	const hn::ScalableTag<float> d;
	const hn::ScalableTag<uint32_t> du;
	const size_t N = hn::Lanes(d);

	const auto vMargin = hn::Set(d, margin);

	uint32_t visibleCount = 0;
	uint32_t i = 0;

	for (; i + N <= count; i += N)
	{
		const auto bMinX = hn::LoadU(d, minX + i);
		const auto bMaxX = hn::LoadU(d, maxX + i);
		const auto bMinY = hn::LoadU(d, minY + i);
		const auto bMaxY = hn::LoadU(d, maxY + i);
		const auto bMinZ = hn::LoadU(d, minZ + i);
		const auto bMaxZ = hn::LoadU(d, maxZ + i);

		auto culledMask = hn::MaskFalse(d);

		for (int p = 0; p < 6; ++p)
		{
			const auto& pl = frustum.planes[p];
			const auto vA = hn::Set(d, pl.a);
			const auto vB = hn::Set(d, pl.b);
			const auto vC = hn::Set(d, pl.c);
			const auto vD = hn::Set(d, pl.d);

			const auto px = (pl.a > 0.0f) ? bMaxX : bMinX;
			const auto py = (pl.b > 0.0f) ? bMaxY : bMinY;
			const auto pz = (pl.c > 0.0f) ? bMaxZ : bMinZ;

			const auto dist = hn::MulAdd(vA, px, hn::MulAdd(vB, py, hn::MulAdd(vC, pz, vD)));
			const auto outside = hn::Lt(dist, vMargin);
			culledMask = hn::Or(culledMask, outside);
		}

		const auto visible = hn::Not(culledMask);
		if (hn::AllFalse(d, visible))
		{
			continue;
		}

		const auto indices = hn::Iota(du, static_cast<uint32_t>(i));
		hn::CompressStore(indices, hn::RebindMask(du, visible), du, outVisibleIndices + visibleCount);
		visibleCount += static_cast<uint32_t>(hn::CountTrue(d, visible));
	}

	for (; i < count; ++i)
	{
		if (frustum.TestAABB(minX[i], minY[i], minZ[i], maxX[i], maxY[i], maxZ[i], margin))
		{
			outVisibleIndices[visibleCount++] = i;
		}
	}

	return visibleCount;
}

}  // namespace Zero::HWY_NAMESPACE
HWY_AFTER_NAMESPACE();

#endif  // ZERO_SRC_MATH_FRUSTUM_HIGHWAY_INL_H_

