#pragma once
#include <Engine/Core.h>

namespace Zero
{
	struct ENGINE_API alignas(16) Mat4
	{
		float m[16];

		Mat4() = default;
		Mat4 operator+(const Mat4& other) const;
		Mat4 operator*(const Mat4& other) const;
		bool operator==(const Mat4& other) const;
	};

	static_assert(alignof(Mat4) == 16);
	static_assert(sizeof(Mat4) == 64);
}
