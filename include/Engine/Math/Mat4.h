#pragma once
#include <Engine/Core.h>
#include <Engine/Math/Vec3.h>
#include <Engine/Math/Vec4.h>
#include <cstddef>

namespace Zero
{
	struct ZERO_API alignas(16) Mat4
	{
		float m[16];

		Mat4() = default;
		static Mat4 Identity() noexcept;

		static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) noexcept;
		static Mat4 Perspective(float fovRadians, float aspect, float zNear, float zFar) noexcept;
		static Mat4 Orthographic(float left, float right, float bottom, float top, float zNear, float zFar) noexcept;

		static Mat4 Translate(const Vec3& v) noexcept;
		static Mat4 Scale(const Vec3& v) noexcept;
		static Mat4 Scale(float s) noexcept;
		static Mat4 Rotate(float radians, const Vec3& axis) noexcept;

		[[nodiscard]] Mat4 Transpose() const noexcept;
		[[nodiscard]] Mat4 Inverse() const noexcept;
		[[nodiscard]] float Determinant() const noexcept;

		Mat4 operator+(const Mat4& other) const noexcept;
		Mat4 operator*(const Mat4& other) const;
		Vec4 operator*(const Vec4& v) const noexcept;
		Vec3 operator*(const Vec3& v) const noexcept;

		bool operator==(const Mat4& other) const noexcept;
		bool operator!=(const Mat4& other) const noexcept;

		static void MultiplyBatch(const Mat4& transform, const Mat4* in, Mat4* out, size_t count) noexcept;
		static void TransformPointsBatch(const Mat4& transform, const Vec3* in, Vec3* out, size_t count) noexcept;
		static void TransformVectorsBatch(const Mat4& transform, const Vec3* in, Vec3* out, size_t count) noexcept;
	};

	[[nodiscard]] inline Mat4 Transpose(const Mat4& m) noexcept { return m.Transpose(); }
	[[nodiscard]] inline Mat4 Inverse(const Mat4& m) noexcept { return m.Inverse(); }

	static_assert(alignof(Mat4) == 16);
	static_assert(sizeof(Mat4) == 64);
}
