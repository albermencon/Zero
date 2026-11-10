#include "pch.h"
#include <Engine/Math/Mat4.h>
#include <cstring>

#define HWY_DISABLED_TARGETS (HWY_AVX3 | (HWY_AVX3 - 1))
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "Math/Mat4Highway-inl.h"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

#include "Math/Mat4Highway-inl.h"

namespace Zero
{
	Mat4 Mat4::Identity() noexcept
	{
		Mat4 res{};
		res.m[0] = 1.0f;
		res.m[5] = 1.0f;
		res.m[10] = 1.0f;
		res.m[15] = 1.0f;
		return res;
	}

	Mat4 Mat4::operator+(const Mat4& other) const
	{
		Mat4 result;
		for (size_t i = 0; i < 16; ++i)
			result.m[i] = m[i] + other.m[i];
		return result;
	}

	bool Mat4::operator==(const Mat4& other) const
	{
		for (size_t i = 0; i < 16; ++i)
		{
			if (m[i] != other.m[i])
				return false;
		}
		return true;
	}

#if HWY_ONCE
	HWY_EXPORT(MultiplyMat4Highway);
	HWY_EXPORT(MultiplyBatchHighway);

	Mat4 Mat4::operator*(const Mat4& other) const
	{
		return HWY_DYNAMIC_DISPATCH(MultiplyMat4Highway)(*this, other);
	}

	void Mat4::MultiplyBatch(const Mat4& transform, const Mat4* in, Mat4* out, size_t count) noexcept
	{
		HWY_DYNAMIC_DISPATCH(MultiplyBatchHighway)(transform, in, out, count);
	}
#endif
}
