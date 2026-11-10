#if defined(ZERO_SRC_MATH_MAT4_HIGHWAY_INL_H_) == defined(HWY_TARGET_TOGGLE)
#ifdef ZERO_SRC_MATH_MAT4_HIGHWAY_INL_H_
#undef ZERO_SRC_MATH_MAT4_HIGHWAY_INL_H_
#else
#define ZERO_SRC_MATH_MAT4_HIGHWAY_INL_H_
#endif

#include <hwy/highway.h>
#include <Engine/Math/Mat4.h>

HWY_BEFORE_NAMESPACE();
namespace Zero::HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

Mat4 MultiplyMat4Highway(const Mat4& a, const Mat4& b)
{
	Mat4 result;
	const hn::FixedTag<float, 4> d;

	const auto a0 = hn::Load(d, &a.m[0]);
	const auto a1 = hn::Load(d, &a.m[4]);
	const auto a2 = hn::Load(d, &a.m[8]);
	const auto a3 = hn::Load(d, &a.m[12]);

	for (int col = 0; col < 4; ++col)
	{
		const auto b0 = hn::Set(d, b.m[col * 4 + 0]);
		const auto b1 = hn::Set(d, b.m[col * 4 + 1]);
		const auto b2 = hn::Set(d, b.m[col * 4 + 2]);
		const auto b3 = hn::Set(d, b.m[col * 4 + 3]);

		const auto c = hn::MulAdd(a0, b0, hn::MulAdd(a1, b1, hn::MulAdd(a2, b2, hn::Mul(a3, b3))));
		hn::Store(c, d, &result.m[col * 4]);
	}
	return result;
}

void MultiplyBatchHighway(const Mat4& transform, const Mat4* in, Mat4* out, size_t count)
{
	const hn::FixedTag<float, 4> d;

	const auto a0 = hn::Load(d, &transform.m[0]);
	const auto a1 = hn::Load(d, &transform.m[4]);
	const auto a2 = hn::Load(d, &transform.m[8]);
	const auto a3 = hn::Load(d, &transform.m[12]);

	for (size_t i = 0; i < count; ++i)
	{
		const Mat4& b = in[i];
		Mat4& res = out[i];

		for (int col = 0; col < 4; ++col)
		{
			const auto b0 = hn::Set(d, b.m[col * 4 + 0]);
			const auto b1 = hn::Set(d, b.m[col * 4 + 1]);
			const auto b2 = hn::Set(d, b.m[col * 4 + 2]);
			const auto b3 = hn::Set(d, b.m[col * 4 + 3]);

			const auto c = hn::MulAdd(a0, b0, hn::MulAdd(a1, b1, hn::MulAdd(a2, b2, hn::Mul(a3, b3))));
			hn::Store(c, d, &res.m[col * 4]);
		}
	}
}

}  // namespace Zero::HWY_NAMESPACE
HWY_AFTER_NAMESPACE();

#endif  // ZERO_SRC_MATH_MAT4_HIGHWAY_INL_H_
