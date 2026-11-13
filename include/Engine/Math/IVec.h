#pragma once
#include <Engine/Core.h>
#include <cstdint>

namespace Zero
{
	struct ZERO_API IVec2
	{
		int32_t x = 0;
		int32_t y = 0;

		constexpr IVec2() = default;
		constexpr IVec2(int32_t x, int32_t y) noexcept : x(x), y(y) {}
		constexpr explicit IVec2(int32_t s) noexcept : x(s), y(s) {}

		constexpr IVec2 operator+(const IVec2& other) const noexcept { return { x + other.x, y + other.y }; }
		constexpr IVec2 operator-(const IVec2& other) const noexcept { return { x - other.x, y - other.y }; }
		constexpr IVec2 operator*(const IVec2& other) const noexcept { return { x * other.x, y * other.y }; }
		constexpr IVec2 operator*(int32_t scalar) const noexcept { return { x * scalar, y * scalar }; }
		constexpr IVec2 operator/(int32_t scalar) const noexcept { return { x / scalar, y / scalar }; }
		constexpr IVec2 operator-() const noexcept { return { -x, -y }; }

		constexpr IVec2& operator+=(const IVec2& other) noexcept { x += other.x; y += other.y; return *this; }
		constexpr IVec2& operator-=(const IVec2& other) noexcept { x -= other.x; y -= other.y; return *this; }
		constexpr IVec2& operator*=(const IVec2& other) noexcept { x *= other.x; y *= other.y; return *this; }
		constexpr IVec2& operator*=(int32_t scalar) noexcept { x *= scalar; y *= scalar; return *this; }
		constexpr IVec2& operator/=(int32_t scalar) noexcept { x /= scalar; y /= scalar; return *this; }

		constexpr bool operator==(const IVec2& other) const noexcept { return x == other.x && y == other.y; }
		constexpr bool operator!=(const IVec2& other) const noexcept { return !(*this == other); }
	};

	constexpr IVec2 operator*(int32_t scalar, const IVec2& v) noexcept { return v * scalar; }

	struct ZERO_API IVec3
	{
		int32_t x = 0;
		int32_t y = 0;
		int32_t z = 0;

		constexpr IVec3() = default;
		constexpr IVec3(int32_t x, int32_t y, int32_t z) noexcept : x(x), y(y), z(z) {}
		constexpr explicit IVec3(int32_t s) noexcept : x(s), y(s), z(s) {}

		constexpr IVec3 operator+(const IVec3& other) const noexcept { return { x + other.x, y + other.y, z + other.z }; }
		constexpr IVec3 operator-(const IVec3& other) const noexcept { return { x - other.x, y - other.y, z - other.z }; }
		constexpr IVec3 operator*(const IVec3& other) const noexcept { return { x * other.x, y * other.y, z * other.z }; }
		constexpr IVec3 operator*(int32_t scalar) const noexcept { return { x * scalar, y * scalar, z * scalar }; }
		constexpr IVec3 operator/(int32_t scalar) const noexcept { return { x / scalar, y / scalar, z / scalar }; }
		constexpr IVec3 operator-() const noexcept { return { -x, -y, -z }; }

		constexpr IVec3& operator+=(const IVec3& other) noexcept { x += other.x; y += other.y; z += other.z; return *this; }
		constexpr IVec3& operator-=(const IVec3& other) noexcept { x -= other.x; y -= other.y; z -= other.z; return *this; }
		constexpr IVec3& operator*=(const IVec3& other) noexcept { x *= other.x; y *= other.y; z *= other.z; return *this; }
		constexpr IVec3& operator*=(int32_t scalar) noexcept { x *= scalar; y *= scalar; z *= scalar; return *this; }
		constexpr IVec3& operator/=(int32_t scalar) noexcept { x /= scalar; y /= scalar; z /= scalar; return *this; }

		constexpr bool operator==(const IVec3& other) const noexcept { return x == other.x && y == other.y && z == other.z; }
		constexpr bool operator!=(const IVec3& other) const noexcept { return !(*this == other); }
	};

	constexpr IVec3 operator*(int32_t scalar, const IVec3& v) noexcept { return v * scalar; }

	struct ZERO_API alignas(16) IVec4
	{
		int32_t x = 0;
		int32_t y = 0;
		int32_t z = 0;
		int32_t w = 0;

		constexpr IVec4() = default;
		constexpr IVec4(int32_t x, int32_t y, int32_t z, int32_t w) noexcept : x(x), y(y), z(z), w(w) {}
		constexpr explicit IVec4(int32_t s) noexcept : x(s), y(s), z(s), w(s) {}

		constexpr IVec4 operator+(const IVec4& other) const noexcept { return { x + other.x, y + other.y, z + other.z, w + other.w }; }
		constexpr IVec4 operator-(const IVec4& other) const noexcept { return { x - other.x, y - other.y, z - other.z, w - other.w }; }
		constexpr IVec4 operator*(const IVec4& other) const noexcept { return { x * other.x, y * other.y, z * other.z, w * other.w }; }
		constexpr IVec4 operator*(int32_t scalar) const noexcept { return { x * scalar, y * scalar, z * scalar, w * scalar }; }
		constexpr IVec4 operator/(int32_t scalar) const noexcept { return { x / scalar, y / scalar, z / scalar, w / scalar }; }
		constexpr IVec4 operator-() const noexcept { return { -x, -y, -z, -w }; }

		constexpr IVec4& operator+=(const IVec4& other) noexcept { x += other.x; y += other.y; z += other.z; w += other.w; return *this; }
		constexpr IVec4& operator-=(const IVec4& other) noexcept { x -= other.x; y -= other.y; z -= other.z; w -= other.w; return *this; }
		constexpr IVec4& operator*=(const IVec4& other) noexcept { x *= other.x; y *= other.y; z *= other.z; w *= other.w; return *this; }
		constexpr IVec4& operator*=(int32_t scalar) noexcept { x *= scalar; y *= scalar; z *= scalar; w *= scalar; return *this; }
		constexpr IVec4& operator/=(int32_t scalar) noexcept { x /= scalar; y /= scalar; z /= scalar; w /= scalar; return *this; }

		constexpr bool operator==(const IVec4& other) const noexcept { return x == other.x && y == other.y && z == other.z && w == other.w; }
		constexpr bool operator!=(const IVec4& other) const noexcept { return !(*this == other); }
	};

	constexpr IVec4 operator*(int32_t scalar, const IVec4& v) noexcept { return v * scalar; }
}

