#pragma once
#include <Engine/Ecs/ComponentTrait.h>
#include <Engine/Math/Vec3.h>
#include <type_traits>

namespace Zero
{
    struct VelocityComponent
    {
        Vec3 linear;
        Vec3 angular;
    };
    static_assert(std::is_trivially_copyable_v<VelocityComponent>);
}

REGISTER_COMPONENT(Zero::VelocityComponent, 4)
