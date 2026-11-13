#pragma once
#include <Engine/Core.h>
#include <cmath>

namespace Zero 
{
	struct ZERO_API Vec3 
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;

		constexpr Vec3() = default;
		constexpr Vec3(float x, float y, float z) noexcept : x(x), y(y), z(z) {}
		constexpr explicit Vec3(float s) noexcept : x(s), y(s), z(s) {}

		constexpr Vec3 operator+(const Vec3& other) const noexcept { return { x + other.x, y + other.y, z + other.z }; }
		constexpr Vec3 operator-(const Vec3& other) const noexcept { return { x - other.x, y - other.y, z - other.z }; }
		constexpr Vec3 operator*(const Vec3& other) const noexcept { return { x * other.x, y * other.y, z * other.z }; }
		constexpr Vec3 operator*(float scalar) const noexcept { return { x * scalar, y * scalar, z * scalar }; }
		constexpr Vec3 operator/(float scalar) const noexcept
		{
			const float inv = 1.0f / scalar;
			return { x * inv, y * inv, z * inv };
		}
		constexpr Vec3 operator-() const noexcept { return { -x, -y, -z }; }

		constexpr Vec3& operator+=(const Vec3& other) noexcept
		{
			x += other.x;
			y += other.y;
			z += other.z;
			return *this;
		}

		constexpr Vec3& operator-=(const Vec3& other) noexcept
		{
			x -= other.x;
			y -= other.y;
			z -= other.z;
			return *this;
		}

		constexpr Vec3& operator*=(const Vec3& other) noexcept
		{
			x *= other.x;
			y *= other.y;
			z *= other.z;
			return *this;
		}

		constexpr Vec3& operator*=(float scalar) noexcept
		{
			x *= scalar;
			y *= scalar;
			z *= scalar;
			return *this;
		}

		constexpr Vec3& operator/=(float scalar) noexcept
		{
			const float inv = 1.0f / scalar;
			x *= inv;
			y *= inv;
			z *= inv;
			return *this;
		}

		constexpr bool operator==(const Vec3& other) const noexcept
		{
			return (x == other.x && y == other.y && z == other.z);
		}

		constexpr bool operator!=(const Vec3& other) const noexcept
		{
			return !(*this == other);
		}

		static constexpr float Dot(const Vec3& a, const Vec3& b) noexcept
		{
			return a.x * b.x + a.y * b.y + a.z * b.z;
		}

		[[nodiscard]] constexpr float Dot(const Vec3& other) const noexcept
		{
			return Dot(*this, other);
		}

		static constexpr Vec3 Cross(const Vec3& a, const Vec3& b) noexcept
		{
			return Vec3(
				a.y * b.z - a.z * b.y,
				a.z * b.x - a.x * b.z,
				a.x * b.y - a.y * b.x
			);
		}

		[[nodiscard]] constexpr Vec3 Cross(const Vec3& other) const noexcept
		{
			return Cross(*this, other);
		}

		[[nodiscard]] float Length() const noexcept
		{
			return std::sqrt(LengthSquared());
		}

		[[nodiscard]] constexpr float LengthSquared() const noexcept
		{
			return x * x + y * y + z * z;
		}

		void Normalize() noexcept
		{
			const float lenSq = LengthSquared();
			if (lenSq > 1e-8f)
			{
				const float invLen = 1.0f / std::sqrt(lenSq);
				x *= invLen;
				y *= invLen;
				z *= invLen;
			}
		}

		[[nodiscard]] Vec3 Normalized() const noexcept
		{
			Vec3 copy = *this;
			copy.Normalize();
			return copy;
		}
	};

	constexpr Vec3 operator*(float scalar, const Vec3& v) noexcept
	{
		return v * scalar;
	}

	constexpr float Dot(const Vec3& a, const Vec3& b) noexcept
	{
		return Vec3::Dot(a, b);
	}

	constexpr Vec3 Cross(const Vec3& a, const Vec3& b) noexcept
	{
		return Vec3::Cross(a, b);
	}
}
