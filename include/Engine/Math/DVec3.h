#pragma once
#include <Engine/Core.h>
#include <Engine/Math/Vec3.h>
#include <cmath>
#include <type_traits>

namespace Zero
{
	struct ZERO_API DVec3
	{
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;

		constexpr DVec3() = default;
		constexpr DVec3(double x, double y, double z) noexcept : x(x), y(y), z(z) {}
		constexpr explicit DVec3(double s) noexcept : x(s), y(s), z(s) {}
		constexpr explicit DVec3(const Vec3& v) noexcept
			: x(static_cast<double>(v.x))
			, y(static_cast<double>(v.y))
			, z(static_cast<double>(v.z))
		{
		}

		constexpr DVec3 operator+(const DVec3& other) const noexcept { return { x + other.x, y + other.y, z + other.z }; }
		constexpr DVec3 operator-(const DVec3& other) const noexcept { return { x - other.x, y - other.y, z - other.z }; }
		constexpr DVec3 operator*(const DVec3& other) const noexcept { return { x * other.x, y * other.y, z * other.z }; }
		constexpr DVec3 operator*(double scalar) const noexcept { return { x * scalar, y * scalar, z * scalar }; }
		constexpr DVec3 operator/(double scalar) const noexcept
		{
			const double inv = 1.0 / scalar;
			return { x * inv, y * inv, z * inv };
		}
		constexpr DVec3 operator-() const noexcept { return { -x, -y, -z }; }

		constexpr DVec3& operator+=(const DVec3& other) noexcept
		{
			x += other.x;
			y += other.y;
			z += other.z;
			return *this;
		}

		constexpr DVec3& operator-=(const DVec3& other) noexcept
		{
			x -= other.x;
			y -= other.y;
			z -= other.z;
			return *this;
		}

		constexpr DVec3& operator*=(const DVec3& other) noexcept
		{
			x *= other.x;
			y *= other.y;
			z *= other.z;
			return *this;
		}

		constexpr DVec3& operator*=(double scalar) noexcept
		{
			x *= scalar;
			y *= scalar;
			z *= scalar;
			return *this;
		}

		constexpr DVec3& operator/=(double scalar) noexcept
		{
			const double inv = 1.0 / scalar;
			x *= inv;
			y *= inv;
			z *= inv;
			return *this;
		}

		constexpr bool operator==(const DVec3& other) const noexcept
		{
			return x == other.x && y == other.y && z == other.z;
		}

		constexpr bool operator!=(const DVec3& other) const noexcept
		{
			return !(*this == other);
		}

		[[nodiscard]] constexpr Vec3 ToVec3() const noexcept
		{
			return Vec3{
				static_cast<float>(x),
				static_cast<float>(y),
				static_cast<float>(z)
			};
		}

		static constexpr double Dot(const DVec3& a, const DVec3& b) noexcept
		{
			return a.x * b.x + a.y * b.y + a.z * b.z;
		}

		[[nodiscard]] constexpr double Dot(const DVec3& other) const noexcept
		{
			return Dot(*this, other);
		}

		static constexpr DVec3 Cross(const DVec3& a, const DVec3& b) noexcept
		{
			return DVec3(
				a.y * b.z - a.z * b.y,
				a.z * b.x - a.x * b.z,
				a.x * b.y - a.y * b.x
			);
		}

		[[nodiscard]] constexpr DVec3 Cross(const DVec3& other) const noexcept
		{
			return Cross(*this, other);
		}

		[[nodiscard]] constexpr double LengthSq() const noexcept
		{
			return x * x + y * y + z * z;
		}

		[[nodiscard]] constexpr double LengthSquared() const noexcept
		{
			return LengthSq();
		}

		[[nodiscard]] double Length() const noexcept
		{
			return std::sqrt(LengthSq());
		}

		void Normalize() noexcept
		{
			const double lenSq = LengthSq();
			if (lenSq > 1e-16)
			{
				const double invLen = 1.0 / std::sqrt(lenSq);
				x *= invLen;
				y *= invLen;
				z *= invLen;
			}
		}

		[[nodiscard]] DVec3 Normalized() const noexcept
		{
			DVec3 copy = *this;
			copy.Normalize();
			return copy;
		}

		[[nodiscard]] static constexpr DVec3 Lerp(const DVec3& a, const DVec3& b, double t) noexcept
		{
			return {
				a.x + (b.x - a.x) * t,
				a.y + (b.y - a.y) * t,
				a.z + (b.z - a.z) * t
			};
		}

		[[nodiscard]] static double Distance(const DVec3& a, const DVec3& b) noexcept
		{
			return (a - b).Length();
		}

		[[nodiscard]] double Distance(const DVec3& other) const noexcept
		{
			return Distance(*this, other);
		}
	};

	static_assert(sizeof(DVec3) == 24);
	static_assert(std::is_trivially_copyable_v<DVec3>);

	constexpr DVec3 operator*(double scalar, const DVec3& v) noexcept
	{
		return v * scalar;
	}

	constexpr double Dot(const DVec3& a, const DVec3& b) noexcept
	{
		return DVec3::Dot(a, b);
	}

	constexpr DVec3 Cross(const DVec3& a, const DVec3& b) noexcept
	{
		return DVec3::Cross(a, b);
	}

	inline double Distance(const DVec3& a, const DVec3& b) noexcept
	{
		return DVec3::Distance(a, b);
	}
}
