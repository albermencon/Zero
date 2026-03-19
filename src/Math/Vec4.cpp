#include "pch.h"
#include <cmath>
#include <Engine/Math/Vec4.h>
#include "Vec4.h"

namespace Zero
{
    Vec4 Vec4::operator+(const Vec4& other) const
    {
        return Vec4(x + other.x, y + other.y, z + other.z, w + other.w);
    }

    Vec4 Vec4::operator-(const Vec4& other) const
    {
        return Vec4(x - other.x, y - other.y, z - other.z, w - other.w);
    }

    bool Vec4::operator==(const Vec4& other) const
    {
        return (x == other.x && y == other.y && z == other.z && w == other.w);
    }

    float Vec4::Dot(const Vec4& a, const Vec4& b)
    {
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    }

    float Vec4::Length() const
    {
        return std::sqrt(LengthSquared());
    }

    float Vec4::LengthSquared() const
    {
        return x * x + y * y + z * z + w * w;
    }
}
