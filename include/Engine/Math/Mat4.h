#pragma once
#include <Engine/Core.h>
#include <cstddef>

namespace Zero
{
	struct ZERO_API alignas(16) Mat4
	{
		float m[16];

		Mat4() = default;
		static Mat4 Identity() noexcept;
		Mat4 operator+(const Mat4& other) const;
		Mat4 operator*(const Mat4& other) const;
		bool operator==(const Mat4& other) const;

		static void MultiplyBatch(const Mat4& transform, const Mat4* in, Mat4* out, size_t count) noexcept;
	};

	static_assert(alignof(Mat4) == 16);
	static_assert(sizeof(Mat4) == 64);
}
