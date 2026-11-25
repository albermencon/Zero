#pragma once
#include <Engine/Ecs/ComponentTrait.h>
#include <Engine/Math/Vec3.h>
#include <Engine/Math/Real.h>
#include <Engine/Math/Quat.h>
#include <Engine/Math/Mat4.h>
#include <type_traits>

namespace Zero
{
    // TRS transform component
    struct alignas(16) TransformComponent
    {
        RVec3 position = {};                    // offset 0  (12B/24B + align pad)
        Quat  rotation = Quat::Identity();      // offset 16/32 (16 bytes, 16-byte aligned)
        Vec3  scale    = { 1.0f, 1.0f, 1.0f };  // offset 32/48 (12 bytes + 4 bytes tail pad)

        [[nodiscard]] static constexpr TransformComponent Identity() noexcept
        {
            return TransformComponent{};
        }

        // Compose column-major world matrix: T * R * S
        [[nodiscard]] Mat4 ToMat4() const noexcept
        {
            Mat4 m = rotation.ToMat4();

            m.m[0]  *= scale.x;
            m.m[1]  *= scale.x;
            m.m[2]  *= scale.x;

            m.m[4]  *= scale.y;
            m.m[5]  *= scale.y;
            m.m[6]  *= scale.y;

            m.m[8]  *= scale.z;
            m.m[9]  *= scale.z;
            m.m[10] *= scale.z;

            const Vec3 pos = ToVec3(position);
            m.m[12] = pos.x;
            m.m[13] = pos.y;
            m.m[14] = pos.z;
            m.m[15] = 1.0f;

            return m;
        }

        // Compose camera-relative column-major matrix: T(position - cameraWorldPos) * R * S
        [[nodiscard]] Mat4 ToMat4Relative(const RVec3& cameraWorldPos) const noexcept
        {
            Mat4 m = rotation.ToMat4();

            m.m[0]  *= scale.x;
            m.m[1]  *= scale.x;
            m.m[2]  *= scale.x;

            m.m[4]  *= scale.y;
            m.m[5]  *= scale.y;
            m.m[6]  *= scale.y;

            m.m[8]  *= scale.z;
            m.m[9]  *= scale.z;
            m.m[10] *= scale.z;

            const Vec3 relPos = ToVec3(position - cameraWorldPos);
            m.m[12] = relPos.x;
            m.m[13] = relPos.y;
            m.m[14] = relPos.z;
            m.m[15] = 1.0f;

            return m;
        }

        // Interpolate between two TRS transforms for alpha in [0, 1].
        [[nodiscard]] static TransformComponent Interpolate(const TransformComponent& prev,
                                                            const TransformComponent& curr,
                                                            float alpha) noexcept
        {
            TransformComponent out;
            out.position = RVec3::Lerp(prev.position, curr.position, static_cast<Real>(alpha));
            out.rotation = Quat::Slerp(prev.rotation, curr.rotation, alpha);
            out.scale    = Vec3::Lerp(prev.scale, curr.scale, alpha);
            return out;
        }

        [[nodiscard]] constexpr bool operator==(const TransformComponent& other) const noexcept
        {
            return position == other.position &&
                   rotation == other.rotation &&
                   scale    == other.scale;
        }

        [[nodiscard]] constexpr bool operator!=(const TransformComponent& other) const noexcept
        {
            return !(*this == other);
        }
    };

    static_assert(std::is_trivially_copyable_v<TransformComponent>);
#ifdef ZERO_DOUBLE_PRECISION
    static_assert(sizeof(TransformComponent) == 64);
#else
    static_assert(sizeof(TransformComponent) == 48);
#endif
    static_assert(alignof(TransformComponent) == 16);
}

REGISTER_COMPONENT(Zero::TransformComponent, 0)
