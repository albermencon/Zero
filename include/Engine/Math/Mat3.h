#pragma once
#include <Engine/Core.h>

namespace Zero
{
	struct ZERO_API Mat3
	{
		float m[9];

		Mat3() = default;
		Mat3 operator+(const Mat3& other) const;
		Mat3 operator*(const Mat3& other) const;
		bool operator==(const Mat3& other) const;
	};
}
