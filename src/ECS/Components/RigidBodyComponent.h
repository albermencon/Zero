#pragma once
#include <Engine/Ecs/ComponentTrait.h>
#include <Engine/Math/Vec3.h>
#include <type_traits>

namespace Zero
{
    // Hot path — written every physics tick.
    // Padding to reach a cache-friendly size without spilling into a second line
    // when packed with adjacent dense-array entries.
    struct RigidBodyComponent
    {
        Vec3  linearVelocity;
        float invMass;
        Vec3  angularVelocity;
        float _pad;                 // explicit — sizeof = 32 bytes
    };
    static_assert(std::is_trivially_copyable_v<RigidBodyComponent>);
    static_assert(sizeof(RigidBodyComponent) == 32);
}

REGISTER_COMPONENT(Zero::RigidBodyComponent, 1)
