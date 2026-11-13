#pragma once
#include <Engine/Core.h>
#include <cmath>

namespace Zero
{
	struct ZERO_API Vec2
	{
		float x = 0.0f;
		float y = 0.0f;

		constexpr Vec2() = default;
		constexpr Vec2(float x, float y) noexcept : x(x), y(y) {}
		constexpr explicit Vec2(float s) noexcept : x(s), y(s) {}

		constexpr Vec2 operator+(const Vec2& other) const noexcept { return { x + other.x, y + other.y }; }
		constexpr Vec2 operator-(const Vec2& other) const noexcept { return { x - other.x, y - other.y }; }
		constexpr Vec2 operator*(const Vec2& other) const noexcept { return { x * other.x, y * other.y }; }
		constexpr Vec2 operator*(float scalar) const noexcept { return { x * scalar, y * scalar }; }
		constexpr Vec2 operator/(float scalar) const noexcept
		{
			const float inv = 1.0f / scalar;
			return { x * inv, y * inv };
		}
		constexpr Vec2 operator-() const noexcept { return { -x, -y }; }

		constexpr Vec2& operator+=(const Vec2& other) noexcept
		{
			x += other.x;
			y += other.y;
			return *this;
		}

		constexpr Vec2& operator-=(const Vec2& other) noexcept
		{
			x -= other.x;
			y -= other.y;
			return *this;
		}

		constexpr Vec2& operator*=(const Vec2& other) noexcept
		{
			x *= other.x;
			y *= other.y;
			return *this;
		}

		constexpr Vec2& operator*=(float scalar) noexcept
		{
			x *= scalar;
			y *= scalar;
			return *this;
		}

		constexpr Vec2& operator/=(float scalar) noexcept
		{
			const float inv = 1.0f / scalar;
			x *= inv;
			y *= inv;
			return *this;
		}

		constexpr bool operator==(const Vec2& other) const noexcept
		{
			return (x == other.x && y == other.y);
		}

		constexpr bool operator!=(const Vec2& other) const noexcept
		{
			return !(*this == other);
		}

		static constexpr float Dot(const Vec2& a, const Vec2& b) noexcept
		{
			return a.x * b.x + a.y * b.y;
		}

		[[nodiscard]] constexpr float Dot(const Vec2& other) const noexcept
		{
			return Dot(*this, other);
		}

		[[nodiscard]] float Length() const noexcept
		{
			return std::sqrt(LengthSquared());
		}

		[[nodiscard]] constexpr float LengthSquared() const noexcept
		{
			return x * x + y * y;
		}

		void Normalize() noexcept
		{
			const float lenSq = LengthSquared();
			if (lenSq > 1e-8f)
			{
				const float invLen = 1.0f / std::sqrt(lenSq);
				x *= invLen;
				y *= invLen;
			}
		}

		[[nodiscard]] Vec2 Normalized() const noexcept
		{
			Vec2 copy = *this;
			copy.Normalize();
			return copy;
		}
	};

	constexpr Vec2 operator*(float scalar, const Vec2& v) noexcept
	{
		return v * scalar;
	}

	constexpr float Dot(const Vec2& a, const Vec2& b) noexcept
	{
		return Vec2::Dot(a, b);
	}
}

