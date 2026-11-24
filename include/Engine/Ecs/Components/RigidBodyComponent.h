#pragma once
#include <Engine/Ecs/ComponentTrait.h>
#include <Engine/Math/Vec3.h>
#include <Engine/Physics/PhysicsSystemAPI.h>
#include <type_traits>

namespace Zero
{
    // Hot path — written every physics tick.
    struct RigidBodyComponent
    {
        Vec3   linearVelocity  = { 0.0f, 0.0f, 0.0f };
        float  invMass         = 0.0f;
        Vec3   angularVelocity = { 0.0f, 0.0f, 0.0f };
        BodyID bodyId          = kInvalidBodyID;      // opaque handle — sizeof = 32 bytes
    };
    static_assert(std::is_trivially_copyable_v<RigidBodyComponent>);
    static_assert(sizeof(RigidBodyComponent) == 32);
}

REGISTER_COMPONENT(Zero::RigidBodyComponent, 1)
