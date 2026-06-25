#pragma once
#include <Engine/Core.h>
#include <Engine/Math/Vec3.h>

namespace Zero
{
    struct ZERO_API Plane
    {
        Vec3 normal;
        float distance;
    };
}
