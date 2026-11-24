#pragma once
#include <Engine/Math/Vec3.h>
#include <Engine/Math/DVec3.h>

namespace Zero
{
#ifdef ZERO_DOUBLE_PRECISION
	using Real = double;
	using RVec3 = DVec3;

	[[nodiscard]] constexpr Vec3 ToVec3(const RVec3& v) noexcept
	{
		return v.ToVec3();
	}

	[[nodiscard]] constexpr RVec3 ToRVec3(const Vec3& v) noexcept
	{
		return DVec3(v);
	}
#else
	using Real = float;
	using RVec3 = Vec3;

	[[nodiscard]] constexpr Vec3 ToVec3(const RVec3& v) noexcept
	{
		return v;
	}

	[[nodiscard]] constexpr RVec3 ToRVec3(const Vec3& v) noexcept
	{
		return v;
	}
#endif
}
