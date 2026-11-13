#pragma once
#include <Engine/Core.h>
#include <Engine/Math/Vec3.h>
#include <Engine/Math/Mat4.h>
#include <cmath>

namespace Zero
{
	struct ZERO_API alignas(16) Quat
	{
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
		float w = 1.0f;

		constexpr Quat() = default;
		constexpr Quat(float x, float y, float z, float w) noexcept : x(x), y(y), z(z), w(w) {}

		static constexpr Quat Identity() noexcept
		{
			return { 0.0f, 0.0f, 0.0f, 1.0f };
		}

		static Quat FromAxisAngle(const Vec3& axis, float angle) noexcept
		{
			const Vec3 normAxis = axis.Normalized();
			const float halfAngle = angle * 0.5f;
			const float s = std::sin(halfAngle);
			return { normAxis.x * s, normAxis.y * s, normAxis.z * s, std::cos(halfAngle) };
		}

		constexpr Quat operator*(const Quat& q) const noexcept
		{
			return {
				w * q.x + x * q.w + y * q.z - z * q.y,
				w * q.y - x * q.z + y * q.w + z * q.x,
				w * q.z + x * q.y - y * q.x + z * q.w,
				w * q.w - x * q.x - y * q.y - z * q.z
			};
		}

		Vec3 operator*(const Vec3& v) const noexcept
		{
			const Vec3 qv(x, y, z);
			const Vec3 t = Vec3::Cross(qv, v) * 2.0f;
			return v + (t * w) + Vec3::Cross(qv, t);
		}

		[[nodiscard]] constexpr float LengthSquared() const noexcept
		{
			return x * x + y * y + z * z + w * w;
		}

		[[nodiscard]] float Length() const noexcept
		{
			return std::sqrt(LengthSquared());
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
			else
			{
				*this = Identity();
			}
		}

		[[nodiscard]] Quat Normalized() const noexcept
		{
			Quat res = *this;
			res.Normalize();
			return res;
		}

		[[nodiscard]] constexpr Quat Conjugate() const noexcept
		{
			return { -x, -y, -z, w };
		}

		[[nodiscard]] Quat Inverse() const noexcept
		{
			const float lenSq = LengthSquared();
			if (lenSq > 1e-8f)
			{
				const float invLenSq = 1.0f / lenSq;
				return { -x * invLenSq, -y * invLenSq, -z * invLenSq, w * invLenSq };
			}
			return Identity();
		}

		[[nodiscard]] Mat4 ToMat4() const noexcept
		{
			Mat4 res = Mat4::Identity();

			const float xx = x * x;
			const float yy = y * y;
			const float zz = z * z;
			const float xy = x * y;
			const float xz = x * z;
			const float yz = y * z;
			const float wx = w * x;
			const float wy = w * y;
			const float wz = w * z;

			// Column 0
			res.m[0] = 1.0f - 2.0f * (yy + zz);
			res.m[1] = 2.0f * (xy + wz);
			res.m[2] = 2.0f * (xz - wy);
			res.m[3] = 0.0f;

			// Column 1
			res.m[4] = 2.0f * (xy - wz);
			res.m[5] = 1.0f - 2.0f * (xx + zz);
			res.m[6] = 2.0f * (yz + wx);
			res.m[7] = 0.0f;

			// Column 2
			res.m[8]  = 2.0f * (xz + wy);
			res.m[9]  = 2.0f * (yz - wx);
			res.m[10] = 1.0f - 2.0f * (xx + yy);
			res.m[11] = 0.0f;

			// Column 3
			res.m[12] = 0.0f;
			res.m[13] = 0.0f;
			res.m[14] = 0.0f;
			res.m[15] = 1.0f;

			return res;
		}

		constexpr bool operator==(const Quat& other) const noexcept
		{
			return x == other.x && y == other.y && z == other.z && w == other.w;
		}

		constexpr bool operator!=(const Quat& other) const noexcept
		{
			return !(*this == other);
		}
	};

	static_assert(sizeof(Quat) == 16);
	static_assert(alignof(Quat) == 16);
}

