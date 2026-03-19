#pragma once
#include <Engine/Core.h>

namespace Zero 
{
	struct ENGINE_API Vec3 
	{
		float x, y, z;

		Vec3 operator+(const Vec3& other) const;
		Vec3 operator-(const Vec3& other) const;
		bool operator==(const Vec3& other) const;

		static float Dot(const Vec3& a, const Vec3& b);
		static Vec3 Cross(const Vec3& a, const Vec3& b);
		float Length() const;
		float LengthSquared() const;
	};
}
