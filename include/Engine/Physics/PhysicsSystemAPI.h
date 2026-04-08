#pragma once
#include "PhysicsTypes.h"
#include <limits>
#include <memory>

namespace Zero {

    struct BodyID {
        uint32_t value = std::numeric_limits<uint32_t>::max();

        [[nodiscard]] constexpr bool IsValid()   const noexcept { return value != std::numeric_limits<uint32_t>::max(); }
        [[nodiscard]] constexpr bool IsInvalid() const noexcept { return value == std::numeric_limits<uint32_t>::max(); }

        constexpr bool operator==(const BodyID&) const noexcept = default;
    };

    inline constexpr BodyID kInvalidBodyID{~0u};

    // Describes a body to be created.
    enum class BodyMotion : uint8_t {
        Static,
        Kinematic,
        Dynamic
    };

    struct RigidBodyDesc {
        float        position[3]    = {0.f, 0.f, 0.f};
        float        rotation[4]    = {0.f, 0.f, 0.f, 1.f};  // quaternion xyzw
        float        halfExtents[3] = {0.5f, 0.5f, 0.5f};    // box half-size
        float        mass           = 1.f;
        ObjectLayer  layer          = ObjectLayer::DYNAMIC;
        BodyMotion   motion         = BodyMotion::Dynamic;
        bool         startActivated = true;
    };
}
