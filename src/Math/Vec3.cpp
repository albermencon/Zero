#include "pch.h"
#include <Engine/Math/Vec3.h>
#include <cmath>

namespace Zero 
{
	Vec3 Vec3::operator+(const Vec3& other) const
	{
		return Vec3(x + other.x, y + other.y, z + other.z);
	}

	Vec3 Vec3::operator-(const Vec3& other) const
	{
		return Vec3(x - other.x, y - other.y, z - other.z);
	}

	bool Vec3::operator==(const Vec3& other) const
	{
		return (x == other.x && y == other.y && z == other.z);
	}

	float Vec3::Dot(const Vec3& a, const Vec3& b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}

	Vec3 Vec3::Cross(const Vec3& a, const Vec3& b)
	{
		return Vec3(
			a.y * b.z - a.z * b.y,
			a.z * b.x - a.x * b.z,
			a.x * b.y - a.y * b.x
		);
	}

	float Vec3::Length() const
	{
		return std::sqrt(LengthSquared());
	}

	float Vec3::LengthSquared() const
	{
		return x * x + y * y + z * z;
	}
}
