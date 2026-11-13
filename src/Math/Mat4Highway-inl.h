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

void TransformPointsBatchHighway(const Mat4& transform, const Vec3* in, Vec3* out, size_t count)
{
	const hn::FixedTag<float, 4> d;

	const auto m0 = hn::Set(d, transform.m[0]);
	const auto m4 = hn::Set(d, transform.m[4]);
	const auto m8 = hn::Set(d, transform.m[8]);
	const auto m12 = hn::Set(d, transform.m[12]);

	const auto m1 = hn::Set(d, transform.m[1]);
	const auto m5 = hn::Set(d, transform.m[5]);
	const auto m9 = hn::Set(d, transform.m[9]);
	const auto m13 = hn::Set(d, transform.m[13]);

	const auto m2 = hn::Set(d, transform.m[2]);
	const auto m6 = hn::Set(d, transform.m[6]);
	const auto m10 = hn::Set(d, transform.m[10]);
	const auto m14 = hn::Set(d, transform.m[14]);

	size_t i = 0;
	for (; i + 4 <= count; i += 4)
	{
		alignas(16) float px[4] = { in[i].x, in[i + 1].x, in[i + 2].x, in[i + 3].x };
		alignas(16) float py[4] = { in[i].y, in[i + 1].y, in[i + 2].y, in[i + 3].y };
		alignas(16) float pz[4] = { in[i].z, in[i + 1].z, in[i + 2].z, in[i + 3].z };

		const auto x = hn::Load(d, px);
		const auto y = hn::Load(d, py);
		const auto z = hn::Load(d, pz);

		const auto rx = hn::MulAdd(m0, x, hn::MulAdd(m4, y, hn::MulAdd(m8, z, m12)));
		const auto ry = hn::MulAdd(m1, x, hn::MulAdd(m5, y, hn::MulAdd(m9, z, m13)));
		const auto rz = hn::MulAdd(m2, x, hn::MulAdd(m6, y, hn::MulAdd(m10, z, m14)));

		alignas(16) float ox[4], oy[4], oz[4];
		hn::Store(rx, d, ox);
		hn::Store(ry, d, oy);
		hn::Store(rz, d, oz);

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

		out[i].x = transform.m[0] * x + transform.m[4] * y + transform.m[8]  * z + transform.m[12];
		out[i].y = transform.m[1] * x + transform.m[5] * y + transform.m[9]  * z + transform.m[13];
		out[i].z = transform.m[2] * x + transform.m[6] * y + transform.m[10] * z + transform.m[14];
	}
}

void TransformVectorsBatchHighway(const Mat4& transform, const Vec3* in, Vec3* out, size_t count)
{
	const hn::FixedTag<float, 4> d;

	const auto m0 = hn::Set(d, transform.m[0]);
	const auto m4 = hn::Set(d, transform.m[4]);
	const auto m8 = hn::Set(d, transform.m[8]);

	const auto m1 = hn::Set(d, transform.m[1]);
	const auto m5 = hn::Set(d, transform.m[5]);
	const auto m9 = hn::Set(d, transform.m[9]);

	const auto m2 = hn::Set(d, transform.m[2]);
	const auto m6 = hn::Set(d, transform.m[6]);
	const auto m10 = hn::Set(d, transform.m[10]);

	size_t i = 0;
	for (; i + 4 <= count; i += 4)
	{
		alignas(16) float px[4] = { in[i].x, in[i + 1].x, in[i + 2].x, in[i + 3].x };
		alignas(16) float py[4] = { in[i].y, in[i + 1].y, in[i + 2].y, in[i + 3].y };
		alignas(16) float pz[4] = { in[i].z, in[i + 1].z, in[i + 2].z, in[i + 3].z };

		const auto x = hn::Load(d, px);
		const auto y = hn::Load(d, py);
		const auto z = hn::Load(d, pz);

		const auto rx = hn::MulAdd(m0, x, hn::MulAdd(m4, y, hn::Mul(m8, z)));
		const auto ry = hn::MulAdd(m1, x, hn::MulAdd(m5, y, hn::Mul(m9, z)));
		const auto rz = hn::MulAdd(m2, x, hn::MulAdd(m6, y, hn::Mul(m10, z)));

		alignas(16) float ox[4], oy[4], oz[4];
		hn::Store(rx, d, ox);
		hn::Store(ry, d, oy);
		hn::Store(rz, d, oz);

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

		out[i].x = transform.m[0] * x + transform.m[4] * y + transform.m[8]  * z;
		out[i].y = transform.m[1] * x + transform.m[5] * y + transform.m[9]  * z;
		out[i].z = transform.m[2] * x + transform.m[6] * y + transform.m[10] * z;
	}
}

}
HWY_AFTER_NAMESPACE();

#endif
