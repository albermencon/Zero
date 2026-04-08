#pragma once
#include <array>
#include <cstdint>
#include <string_view>

namespace Zero {

    enum class ObjectLayer : uint8_t {
        STATIC = 0,
        DYNAMIC = 1,
        KINEMATIC = 2,
        PROJECTILE = 3,
        TRIGGER = 4,
        RAGDOLL = 5,
        
        COUNT,             // Do not use as a layer value
        NONE = 0xFF        // "No layer" sentinel
    };

    // Total number of defined layers
    static constexpr uint32_t kMaxObjectLayers = static_cast<uint32_t>(ObjectLayer::COUNT);

    // Bitmask: bit N = ObjectLayer(N) participates in the collision
    using LayerMask = uint32_t;

    template<typename... Ts>
    constexpr LayerMask MakeMask(Ts... layers) noexcept {
        static_assert((std::is_same_v<Ts, ObjectLayer> && ...),
                    "MakeMask: all arguments must be ObjectLayer");
        return ((1u << static_cast<uint8_t>(layers)) | ...);
    }

    struct LayerConfig {
        std::string_view name;          // Debug / profiler name
        LayerMask        collidesWith;  // Bitmask of layers this one collides with
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
        static PhysicsSystemConfig Default();
    };
}
