#pragma once
#include <Engine/Ecs/ComponentTrait.h>
#include <Engine/Ecs/Components/TransformComponent.h>
#include <type_traits>

namespace Zero
{
    // Paired previous-step TRS transform for fixed-timestep render interpolation.
    // Snapshotted at the start of each FixedStep before physics advances TransformComponent.
    // Only dynamic / interpolated entities need this component; static meshes omit it.
    struct alignas(16) PreviousTransformComponent
    {
        Vec3 position = { 0.0f, 0.0f, 0.0f };
        Quat rotation = Quat::Identity();
        Vec3 scale    = { 1.0f, 1.0f, 1.0f };

        [[nodiscard]] static constexpr PreviousTransformComponent FromTransform(const TransformComponent& tf) noexcept
        {
            return PreviousTransformComponent{ tf.position, tf.rotation, tf.scale };
        }

        [[nodiscard]] constexpr TransformComponent ToTransform() const noexcept
        {
            return TransformComponent{ position, rotation, scale };
        }

        [[nodiscard]] TransformComponent InterpolateTo(const TransformComponent& curr, float alpha) const noexcept
        {
            return TransformComponent::Interpolate(ToTransform(), curr, alpha);
        }

        [[nodiscard]] constexpr bool DiffersFrom(const TransformComponent& curr) const noexcept
        {
            return position != curr.position ||
                   rotation != curr.rotation ||
                   scale    != curr.scale;
        }
    };

    static_assert(std::is_trivially_copyable_v<PreviousTransformComponent>);
    static_assert(sizeof(PreviousTransformComponent) == 48);
    static_assert(alignof(PreviousTransformComponent) == 16);
}

REGISTER_COMPONENT(Zero::PreviousTransformComponent, 9)
