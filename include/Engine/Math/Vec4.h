#pragma once
#include <Engine/Core.h>

namespace Zero
{
	struct ENGINE_API Vec4
	{
		float x, y, z, w;

		Vec4 operator+(const Vec4& other) const;
		Vec4 operator-(const Vec4& other) const;
		bool operator==(const Vec4& other) const;

		static float Dot(const Vec4& a, const Vec4& b);
		float Length() const;
		float LengthSquared() const;
	};
}
