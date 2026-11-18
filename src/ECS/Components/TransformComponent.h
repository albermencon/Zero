#pragma once
#include <Engine/Ecs/ComponentTrait.h>
#include <Engine/Math/Mat4.h>
#include <type_traits>

namespace Zero
{
    struct TransformComponent
    {
        Mat4 localToWorld;
    };
    static_assert(std::is_trivially_copyable_v<TransformComponent>);
}

REGISTER_COMPONENT(Zero::TransformComponent, 0)
