#pragma once
#include <Engine/Core.h>
#include <Engine/Math/Vec3.h>
#include <cmath>

namespace Zero
{
	struct ZERO_API Vec4
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float w = 0.0f;

		constexpr Vec4() = default;
		constexpr Vec4(float x, float y, float z, float w) noexcept : x(x), y(y), z(z), w(w) {}
		constexpr explicit Vec4(float s) noexcept : x(s), y(s), z(s), w(s) {}
		constexpr Vec4(const Vec3& v, float w) noexcept : x(v.x), y(v.y), z(v.z), w(w) {}

		constexpr Vec4 operator+(const Vec4& other) const noexcept { return { x + other.x, y + other.y, z + other.z, w + other.w }; }
		constexpr Vec4 operator-(const Vec4& other) const noexcept { return { x - other.x, y - other.y, z - other.z, w - other.w }; }
		constexpr Vec4 operator*(const Vec4& other) const noexcept { return { x * other.x, y * other.y, z * other.z, w * other.w }; }
		constexpr Vec4 operator*(float scalar) const noexcept { return { x * scalar, y * scalar, z * scalar, w * scalar }; }
		constexpr Vec4 operator/(float scalar) const noexcept
		{
			const float inv = 1.0f / scalar;
			return { x * inv, y * inv, z * inv, w * inv };
		}
		constexpr Vec4 operator-() const noexcept { return { -x, -y, -z, -w }; }

		constexpr Vec4& operator+=(const Vec4& other) noexcept
		{
			x += other.x; y += other.y; z += other.z; w += other.w;
			return *this;
		}

		constexpr Vec4& operator-=(const Vec4& other) noexcept
		{
			x -= other.x; y -= other.y; z -= other.z; w -= other.w;
			return *this;
		}

		constexpr Vec4& operator*=(const Vec4& other) noexcept
		{
			x *= other.x; y *= other.y; z *= other.z; w *= other.w;
			return *this;
		}

		constexpr Vec4& operator*=(float scalar) noexcept
		{
			x *= scalar; y *= scalar; z *= scalar; w *= scalar;
			return *this;
		}

		constexpr Vec4& operator/=(float scalar) noexcept
		{
			const float inv = 1.0f / scalar;
			x *= inv; y *= inv; z *= inv; w *= inv;
			return *this;
		}

		constexpr bool operator==(const Vec4& other) const noexcept
		{
			return (x == other.x && y == other.y && z == other.z && w == other.w);
		}

		constexpr bool operator!=(const Vec4& other) const noexcept
		{
			return !(*this == other);
		}

		static constexpr float Dot(const Vec4& a, const Vec4& b) noexcept
		{
			return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
		}

		[[nodiscard]] constexpr float Dot(const Vec4& other) const noexcept
		{
			return Dot(*this, other);
		}

		[[nodiscard]] float Length() const noexcept
		{
			return std::sqrt(LengthSquared());
		}

		[[nodiscard]] constexpr float LengthSquared() const noexcept
		{
			return x * x + y * y + z * z + w * w;
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
				w *= invLen;
			}
		}

		[[nodiscard]] Vec4 Normalized() const noexcept
		{
			Vec4 copy = *this;
			copy.Normalize();
			return copy;
		}
	};

	constexpr Vec4 operator*(float scalar, const Vec4& v) noexcept
	{
		return v * scalar;
	}

	constexpr float Dot(const Vec4& a, const Vec4& b) noexcept
	{
		return Vec4::Dot(a, b);
	}
}
