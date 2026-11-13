#include "pch.h"
#include <Engine/Math/Mat4.h>
#include <cmath>
#include <cstring>

#define HWY_DISABLED_TARGETS (HWY_AVX3 | (HWY_AVX3 - 1))
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "Math/Mat4Highway-inl.h"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

#include "Math/Mat4Highway-inl.h"

namespace Zero
{
	Mat4 Mat4::Identity() noexcept
	{
		Mat4 res{};
		res.m[0] = 1.0f;
		res.m[5] = 1.0f;
		res.m[10] = 1.0f;
		res.m[15] = 1.0f;
		return res;
	}

	Mat4 Mat4::LookAt(const Vec3& eye, const Vec3& target, const Vec3& up) noexcept
	{
		const Vec3 f = (target - eye).Normalized();
		const Vec3 s = Vec3::Cross(f, up).Normalized();
		const Vec3 u = Vec3::Cross(s, f);

		Mat4 res = Mat4::Identity();
		res.m[0] = s.x;
		res.m[4] = s.y;
		res.m[8] = s.z;

		res.m[1] = u.x;
		res.m[5] = u.y;
		res.m[9] = u.z;

		res.m[2] = -f.x;
		res.m[6] = -f.y;
		res.m[10] = -f.z;

		res.m[12] = -Vec3::Dot(s, eye);
		res.m[13] = -Vec3::Dot(u, eye);
		res.m[14] = Vec3::Dot(f, eye);

		return res;
	}

	Mat4 Mat4::Perspective(float fovRadians, float aspect, float zNear, float zFar) noexcept
	{
		Mat4 res{};
		const float tanHalfFov = std::tan(fovRadians * 0.5f);

		res.m[0] = 1.0f / (aspect * tanHalfFov);
		res.m[5] = -1.0f / tanHalfFov;
		res.m[10] = zFar / (zNear - zFar);
		res.m[11] = -1.0f;
		res.m[14] = (zFar * zNear) / (zNear - zFar);

		return res;
	}

	Mat4 Mat4::Orthographic(float left, float right, float bottom, float top, float zNear, float zFar) noexcept
	{
		Mat4 res{};
		res.m[0] = 2.0f / (right - left);
		res.m[5] = -2.0f / (top - bottom);
		res.m[10] = 1.0f / (zNear - zFar);
		res.m[12] = -(right + left) / (right - left);
		res.m[13] = (top + bottom) / (top - bottom);
		res.m[14] = zNear / (zNear - zFar);
		res.m[15] = 1.0f;
		return res;
	}

	Mat4 Mat4::Translate(const Vec3& v) noexcept
	{
		Mat4 res = Mat4::Identity();
		res.m[12] = v.x;
		res.m[13] = v.y;
		res.m[14] = v.z;
		return res;
	}

	Mat4 Mat4::Scale(const Vec3& v) noexcept
	{
		Mat4 res{};
		res.m[0] = v.x;
		res.m[5] = v.y;
		res.m[10] = v.z;
		res.m[15] = 1.0f;
		return res;
	}

	Mat4 Mat4::Scale(float s) noexcept
	{
		return Scale(Vec3(s, s, s));
	}

	Mat4 Mat4::Rotate(float radians, const Vec3& axis) noexcept
	{
		const Vec3 u = axis.Normalized();
		const float c = std::cos(radians);
		const float s = std::sin(radians);
		const float oneMinusC = 1.0f - c;

		Mat4 res = Mat4::Identity();
		res.m[0] = c + u.x * u.x * oneMinusC;
		res.m[1] = u.y * u.x * oneMinusC + u.z * s;
		res.m[2] = u.z * u.x * oneMinusC - u.y * s;

		res.m[4] = u.x * u.y * oneMinusC - u.z * s;
		res.m[5] = c + u.y * u.y * oneMinusC;
		res.m[6] = u.z * u.y * oneMinusC + u.x * s;

		res.m[8] = u.x * u.z * oneMinusC + u.y * s;
		res.m[9] = u.y * u.z * oneMinusC - u.x * s;
		res.m[10] = c + u.z * u.z * oneMinusC;

		return res;
	}

	Mat4 Mat4::Transpose() const noexcept
	{
		Mat4 res;
		res.m[0]  = m[0];  res.m[1]  = m[4];  res.m[2]  = m[8];   res.m[3]  = m[12];
		res.m[4]  = m[1];  res.m[5]  = m[5];  res.m[6]  = m[9];   res.m[7]  = m[13];
		res.m[8]  = m[2];  res.m[9]  = m[6];  res.m[10] = m[10];  res.m[11] = m[14];
		res.m[12] = m[3];  res.m[13] = m[7];  res.m[14] = m[11];  res.m[15] = m[15];
		return res;
	}

	float Mat4::Determinant() const noexcept
	{
		const float b00 = m[0] * m[5] - m[1] * m[4];
		const float b01 = m[0] * m[6] - m[2] * m[4];
		const float b02 = m[0] * m[7] - m[3] * m[4];
		const float b03 = m[1] * m[6] - m[2] * m[5];
		const float b04 = m[1] * m[7] - m[3] * m[5];
		const float b05 = m[2] * m[7] - m[3] * m[6];
		const float b06 = m[8] * m[13] - m[9] * m[12];
		const float b07 = m[8] * m[14] - m[10] * m[12];
		const float b08 = m[8] * m[15] - m[11] * m[12];
		const float b09 = m[9] * m[14] - m[10] * m[13];
		const float b10 = m[9] * m[15] - m[11] * m[13];
		const float b11 = m[10] * m[15] - m[11] * m[14];

		return b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
	}

	Mat4 Mat4::Inverse() const noexcept
	{
		const float b00 = m[0] * m[5] - m[1] * m[4];
		const float b01 = m[0] * m[6] - m[2] * m[4];
		const float b02 = m[0] * m[7] - m[3] * m[4];
		const float b03 = m[1] * m[6] - m[2] * m[5];
		const float b04 = m[1] * m[7] - m[3] * m[5];
		const float b05 = m[2] * m[7] - m[3] * m[6];
		const float b06 = m[8] * m[13] - m[9] * m[12];
		const float b07 = m[8] * m[14] - m[10] * m[12];
		const float b08 = m[8] * m[15] - m[11] * m[12];
		const float b09 = m[9] * m[14] - m[10] * m[13];
		const float b10 = m[9] * m[15] - m[11] * m[13];
		const float b11 = m[10] * m[15] - m[11] * m[14];

		const float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
		if (std::abs(det) < 1e-8f)
		{
			return Mat4::Identity();
		}

		const float invDet = 1.0f / det;
		Mat4 inv;
		inv.m[0]  = ( m[5]  * b11 - m[6]  * b10 + m[7]  * b09) * invDet;
		inv.m[1]  = (-m[1]  * b11 + m[2]  * b10 - m[3]  * b09) * invDet;
		inv.m[2]  = ( m[13] * b05 - m[14] * b04 + m[15] * b03) * invDet;
		inv.m[3]  = (-m[9]  * b05 + m[10] * b04 - m[11] * b03) * invDet;

		inv.m[4]  = (-m[4]  * b11 + m[6]  * b08 - m[7]  * b07) * invDet;
		inv.m[5]  = ( m[0]  * b11 - m[2]  * b08 + m[3]  * b07) * invDet;
		inv.m[6]  = (-m[12] * b05 + m[14] * b02 - m[15] * b01) * invDet;
		inv.m[7]  = ( m[8]  * b05 - m[10] * b02 + m[11] * b01) * invDet;

		inv.m[8]  = ( m[4]  * b10 - m[5]  * b08 + m[7]  * b06) * invDet;
		inv.m[9]  = (-m[0]  * b10 + m[1]  * b08 - m[3]  * b06) * invDet;
		inv.m[10] = ( m[12] * b04 - m[13] * b02 + m[15] * b00) * invDet;
		inv.m[11] = (-m[8]  * b04 + m[9]  * b02 - m[11] * b00) * invDet;

		inv.m[12] = (-m[4]  * b09 + m[5]  * b07 - m[6]  * b06) * invDet;
		inv.m[13] = ( m[0]  * b09 - m[1]  * b07 + m[2]  * b06) * invDet;
		inv.m[14] = (-m[12] * b03 + m[13] * b01 - m[14] * b00) * invDet;
		inv.m[15] = ( m[8]  * b03 - m[9]  * b01 + m[10] * b00) * invDet;

		return inv;
	}

	Vec4 Mat4::operator*(const Vec4& v) const noexcept
	{
		return Vec4(
			m[0] * v.x + m[4] * v.y + m[8]  * v.z + m[12] * v.w,
			m[1] * v.x + m[5] * v.y + m[9]  * v.z + m[13] * v.w,
			m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
			m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w
		);
	}

	Vec3 Mat4::operator*(const Vec3& v) const noexcept
	{
		const float w = m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15];
		const float rx = m[0] * v.x + m[4] * v.y + m[8]  * v.z + m[12];
		const float ry = m[1] * v.x + m[5] * v.y + m[9]  * v.z + m[13];
		const float rz = m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14];

		if (w != 1.0f && std::abs(w) > 1e-7f)
		{
			const float invW = 1.0f / w;
			return Vec3(rx * invW, ry * invW, rz * invW);
		}
		return Vec3(rx, ry, rz);
	}

	Mat4 Mat4::operator+(const Mat4& other) const noexcept
	{
		Mat4 result;
		for (size_t i = 0; i < 16; ++i)
			result.m[i] = m[i] + other.m[i];
		return result;
	}

	bool Mat4::operator==(const Mat4& other) const noexcept
	{
		for (size_t i = 0; i < 16; ++i)
		{
			if (m[i] != other.m[i])
				return false;
		}
		return true;
	}

	bool Mat4::operator!=(const Mat4& other) const noexcept
	{
		return !(*this == other);
	}

#if HWY_ONCE
	HWY_EXPORT(MultiplyMat4Highway);
	HWY_EXPORT(MultiplyBatchHighway);
	HWY_EXPORT(TransformPointsBatchHighway);
	HWY_EXPORT(TransformVectorsBatchHighway);

	Mat4 Mat4::operator*(const Mat4& other) const
	{
		return HWY_DYNAMIC_DISPATCH(MultiplyMat4Highway)(*this, other);
	}

	void Mat4::MultiplyBatch(const Mat4& transform, const Mat4* in, Mat4* out, size_t count) noexcept
	{
		HWY_DYNAMIC_DISPATCH(MultiplyBatchHighway)(transform, in, out, count);
	}

	void Mat4::TransformPointsBatch(const Mat4& transform, const Vec3* in, Vec3* out, size_t count) noexcept
	{
		HWY_DYNAMIC_DISPATCH(TransformPointsBatchHighway)(transform, in, out, count);
	}

	void Mat4::TransformVectorsBatch(const Mat4& transform, const Vec3* in, Vec3* out, size_t count) noexcept
	{
		HWY_DYNAMIC_DISPATCH(TransformVectorsBatchHighway)(transform, in, out, count);
	}
#endif
}
