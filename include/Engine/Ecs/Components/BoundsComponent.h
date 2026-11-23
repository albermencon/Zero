#pragma once
#include <Engine/Ecs/ComponentTrait.h>
#include <Engine/Math/Vec3.h>
#include <type_traits>

namespace Zero
{
    struct BoundsComponent
    {
        Vec3 min;
        Vec3 max;
    };
    static_assert(std::is_trivially_copyable_v<BoundsComponent>);
}

REGISTER_COMPONENT(Zero::BoundsComponent, 3)
