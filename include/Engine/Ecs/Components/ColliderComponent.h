#pragma once
#include <Engine/Ecs/ComponentTrait.h>
#include <Engine/Math/Vec3.h>
#include <Engine/Physics/PhysicsSystemAPI.h>
#include <type_traits>

namespace Zero
{
    enum class ColliderShapeType : uint8_t
    {
        Box     = 0,
        Sphere  = 1,
        Capsule = 2,
        Custom  = 3
    };

    // Cold-to-warm collider description used when PhysicsSyncSystem instantiates a
    // Jolt body for an entity carrying RigidBodyComponent.
    struct ColliderComponent
    {
        ShapeID           shape          = InvalidShapeID;
        Vec3              halfExtents    = { 0.5f, 0.5f, 0.5f };
        float             radius         = 0.5f;
        float             halfHeight     = 0.5f;
        float             mass           = 1.0f;
        float             friction       = 0.5f;
        float             restitution    = 0.0f;
        float             linearDamping  = 0.05f;
        float             angularDamping = 0.05f;
        float             gravityFactor  = 1.0f;
        ColliderShapeType shapeType      = ColliderShapeType::Box;
        MotionType        motion         = MotionType::Dynamic;
        PhysicsLayer      layer          = PhysicsLayer::Dynamic;
        bool              isSensor       = false;
        bool              startActivated = true;
        bool              allowSleeping  = true;

        [[nodiscard]] static constexpr ColliderComponent Box(
            const Vec3& halfExtents,
            MotionType motion = MotionType::Dynamic,
            PhysicsLayer layer = PhysicsLayer::Dynamic) noexcept
        {
            ColliderComponent c;
            c.shapeType   = ColliderShapeType::Box;
            c.halfExtents = halfExtents;
            c.motion      = motion;
            c.layer       = layer;
            return c;
        }

        [[nodiscard]] static constexpr ColliderComponent Sphere(
            float radius,
            MotionType motion = MotionType::Dynamic,
            PhysicsLayer layer = PhysicsLayer::Dynamic) noexcept
        {
            ColliderComponent c;
            c.shapeType = ColliderShapeType::Sphere;
            c.radius    = radius;
            c.motion    = motion;
            c.layer     = layer;
            return c;
        }

        [[nodiscard]] static constexpr ColliderComponent Capsule(
            float halfHeight,
            float radius,
            MotionType motion = MotionType::Dynamic,
            PhysicsLayer layer = PhysicsLayer::Dynamic) noexcept
        {
            ColliderComponent c;
            c.shapeType  = ColliderShapeType::Capsule;
            c.halfHeight = halfHeight;
            c.radius     = radius;
            c.motion     = motion;
            c.layer      = layer;
            return c;
        }

        [[nodiscard]] static constexpr ColliderComponent FromShape(
            ShapeID shape,
            MotionType motion = MotionType::Dynamic,
            PhysicsLayer layer = PhysicsLayer::Dynamic) noexcept
        {
            ColliderComponent c;
            c.shapeType = ColliderShapeType::Custom;
            c.shape     = shape;
            c.motion    = motion;
            c.layer     = layer;
            return c;
        }
    };

    static_assert(std::is_trivially_copyable_v<ColliderComponent>);
}

REGISTER_COMPONENT(Zero::ColliderComponent, 10)
