#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <Engine/Export.h>

namespace Zero {

    enum class ObjectLayer : uint8_t {
        STATIC     = 0,
        DYNAMIC    = 1,
        KINEMATIC  = 2,
        PROJECTILE = 3,
        TRIGGER    = 4,
        RAGDOLL    = 5,

        COUNT,             // Do not use as a layer value
        NONE = 0xFF        // "No layer" sentinel
    };

    enum class PhysicsLayer : uint8_t {
        Static     = static_cast<uint8_t>(ObjectLayer::STATIC),
        Dynamic    = static_cast<uint8_t>(ObjectLayer::DYNAMIC),
        Kinematic  = static_cast<uint8_t>(ObjectLayer::KINEMATIC),
        Projectile = static_cast<uint8_t>(ObjectLayer::PROJECTILE),
        Trigger    = static_cast<uint8_t>(ObjectLayer::TRIGGER),
        Ragdoll    = static_cast<uint8_t>(ObjectLayer::RAGDOLL),

        Count      = static_cast<uint8_t>(ObjectLayer::COUNT),
        None       = static_cast<uint8_t>(ObjectLayer::NONE)
    };

    [[nodiscard]] constexpr ObjectLayer ToObjectLayer(PhysicsLayer layer) noexcept {
        return static_cast<ObjectLayer>(static_cast<uint8_t>(layer));
    }

    [[nodiscard]] constexpr PhysicsLayer ToPhysicsLayer(ObjectLayer layer) noexcept {
        return static_cast<PhysicsLayer>(static_cast<uint8_t>(layer));
    }

    // Total number of defined layers
    static constexpr uint32_t kMaxObjectLayers = static_cast<uint32_t>(ObjectLayer::COUNT);

    // Bitmask: bit N = ObjectLayer(N) / PhysicsLayer(N) participates in the collision/query
    using LayerMask = uint32_t;
    inline constexpr LayerMask kAllLayersMask = 0xFFFFFFFFu;

    template<typename... Ts>
    [[nodiscard]] constexpr LayerMask MakeMask(Ts... layers) noexcept {
        static_assert(
            ((std::is_same_v<Ts, ObjectLayer> || std::is_same_v<Ts, PhysicsLayer>) && ...),
            "MakeMask: all arguments must be ObjectLayer or PhysicsLayer");
        return ((1u << static_cast<uint8_t>(layers)) | ...);
    }

    struct LayerConfig {
        std::string_view name;          // Debug / profiler name
        LayerMask        collidesWith;  // Bitmask of layers this one collides with
    };

    enum class MotionType : uint8_t {
        Static    = 0,
        Kinematic = 1,
        Dynamic   = 2
    };

    // Backward-compatible alias
    using BodyMotion = MotionType;

    enum class ContactType : uint8_t {
        Added        = 0,
        Persisted    = 1,
        Removed      = 2,
        TriggerEnter = 3,
        TriggerExit  = 4
    };

    struct PhysicsSystemConfig {
        // World capacity
        uint32_t maxBodies               = 65'536;
        uint32_t maxBodyPairs            = 65'536;
        uint32_t maxContactConstraints   = 10'240;
        uint32_t numBodyMutexes          = 0; // auto detect

        // Threading
        uint32_t numWorkerThreads        = 0;
        uint32_t tempAllocatorSizeMB     = 10;

        // Simulation
        uint32_t collisionSteps          = 1;        // Sub-steps per Update()
        float    fixedTimestep           = 1.0f / 60.0f;
        float    gravity[3]              = {0.0f, -9.81f, 0.0f};

        // Build the table with MakeMask() helpers or call Default().
        std::array<LayerConfig, kMaxObjectLayers> layers{};

        // Returns a ready-to-use config with sensible collision rules for a
        // standard action game. Override specific fields after calling Default().
        [[nodiscard]] static inline PhysicsSystemConfig Default() noexcept {
            PhysicsSystemConfig cfg;
            cfg.layers[static_cast<size_t>(ObjectLayer::STATIC)] = {
                "Static",
                MakeMask(ObjectLayer::DYNAMIC, ObjectLayer::KINEMATIC, ObjectLayer::PROJECTILE, ObjectLayer::RAGDOLL)
            };
            cfg.layers[static_cast<size_t>(ObjectLayer::DYNAMIC)] = {
                "Dynamic",
                MakeMask(ObjectLayer::STATIC, ObjectLayer::DYNAMIC, ObjectLayer::KINEMATIC,
                         ObjectLayer::PROJECTILE, ObjectLayer::TRIGGER, ObjectLayer::RAGDOLL)
            };
            cfg.layers[static_cast<size_t>(ObjectLayer::KINEMATIC)] = {
                "Kinematic",
                MakeMask(ObjectLayer::STATIC, ObjectLayer::DYNAMIC, ObjectLayer::KINEMATIC,
                         ObjectLayer::PROJECTILE, ObjectLayer::TRIGGER, ObjectLayer::RAGDOLL)
            };
            cfg.layers[static_cast<size_t>(ObjectLayer::PROJECTILE)] = {
                "Projectile",
                MakeMask(ObjectLayer::STATIC, ObjectLayer::DYNAMIC, ObjectLayer::KINEMATIC,
                         ObjectLayer::TRIGGER, ObjectLayer::RAGDOLL)
            };
            cfg.layers[static_cast<size_t>(ObjectLayer::TRIGGER)] = {
                "Trigger",
                MakeMask(ObjectLayer::DYNAMIC, ObjectLayer::KINEMATIC, ObjectLayer::PROJECTILE, ObjectLayer::RAGDOLL)
            };
            cfg.layers[static_cast<size_t>(ObjectLayer::RAGDOLL)] = {
                "Ragdoll",
                MakeMask(ObjectLayer::STATIC, ObjectLayer::DYNAMIC, ObjectLayer::KINEMATIC,
                         ObjectLayer::PROJECTILE, ObjectLayer::TRIGGER, ObjectLayer::RAGDOLL)
            };
            return cfg;
        }
    };

}
