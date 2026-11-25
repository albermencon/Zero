#pragma once
#include <Engine/Export.h>
#include <Engine/Physics/PhysicsTypes.h>
#include <Engine/Math/Vec3.h>
#include <Engine/Math/Real.h>
#include <Engine/Math/Quat.h>
#include <Engine/Ecs/Entity.h>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>

namespace Zero 
{
    // Opaque 32-bit physics handles (no Jolt types exposed in public headers)

    struct BodyID {
        uint32_t value = std::numeric_limits<uint32_t>::max();

        [[nodiscard]] constexpr bool IsValid()   const noexcept { return value != std::numeric_limits<uint32_t>::max(); }
        [[nodiscard]] constexpr bool IsInvalid() const noexcept { return value == std::numeric_limits<uint32_t>::max(); }

        constexpr bool operator==(const BodyID&) const noexcept = default;
    };

    inline constexpr BodyID InvalidBodyID{~0u};
    inline constexpr BodyID kInvalidBodyID = InvalidBodyID;

    struct ShapeID {
        uint32_t value = std::numeric_limits<uint32_t>::max();

        [[nodiscard]] constexpr bool IsValid()   const noexcept { return value != std::numeric_limits<uint32_t>::max(); }
        [[nodiscard]] constexpr bool IsInvalid() const noexcept { return value == std::numeric_limits<uint32_t>::max(); }

        constexpr bool operator==(const ShapeID&) const noexcept = default;
    };

    inline constexpr ShapeID InvalidShapeID{~0u};

    // Shape & Body descriptors

    struct CompoundSubShape {
        ShapeID shape    = InvalidShapeID;
        Vec3    position = { 0.0f, 0.0f, 0.0f };
        Quat    rotation = Quat::Identity();
    };

    struct BodyDesc {
        MotionType   motion              = MotionType::Dynamic;
        ShapeID      shape               = InvalidShapeID;
        RVec3        position            = {};
        Quat         rotation            = Quat::Identity();
        Vec3         scale               = { 1.0f, 1.0f, 1.0f };
        Vec3         fallbackHalfExtents = { 0.5f, 0.5f, 0.5f }; // Used if shape == InvalidShapeID
        Vec3         linearVelocity      = { 0.0f, 0.0f, 0.0f };
        Vec3         angularVelocity     = { 0.0f, 0.0f, 0.0f };
        float        mass                = 1.0f;
        float        restitution         = 0.0f;
        float        friction            = 0.5f;
        float        linearDamping       = 0.05f;
        float        angularDamping      = 0.05f;
        float        gravityFactor       = 1.0f;
        PhysicsLayer layer               = PhysicsLayer::Dynamic;
        Entity       userData            = Invalid;
        bool         isSensor            = false;
        bool         startActivated      = true;
        bool         allowSleeping       = true;
    };

    // Legacy stub descriptor kept for compatibility.
    struct RigidBodyDesc {
        float        position[3]    = {0.f, 0.f, 0.f};
        float        rotation[4]    = {0.f, 0.f, 0.f, 1.f};
        float        halfExtents[3] = {0.5f, 0.5f, 0.5f};
        float        mass           = 1.f;
        ObjectLayer  layer          = ObjectLayer::DYNAMIC;
        BodyMotion   motion         = BodyMotion::Dynamic;
        bool         startActivated = true;
    };

    // World configuration & query / event data

    struct PhysicsWorldConfig {
        uint32_t maxBodies             = 65'536;
        uint32_t numBodyMutexes        = 0;                 // 0 = auto-detect
        uint32_t maxBodyPairs          = 65'536;
        uint32_t maxContactConstraints = 10'240;
        uint32_t tempAllocatorBytes    = 10u * 1024u * 1024u;
        uint32_t maxContactEvents      = 4096;
        bool     assertOnContactOverflow = true;
        uint32_t maxShapes             = 16'384;
        uint32_t maxJoltJobs           = 2048;
        uint32_t maxJoltBarriers       = 16;
        Vec3     gravity               = { 0.0f, -9.81f, 0.0f };
        std::array<LayerConfig, kMaxObjectLayers> layers{};

        [[nodiscard]] static inline PhysicsWorldConfig Default() noexcept {
            PhysicsWorldConfig cfg;
            const auto sysDefaults = PhysicsSystemConfig::Default();
            cfg.maxBodies             = sysDefaults.maxBodies;
            cfg.numBodyMutexes        = sysDefaults.numBodyMutexes;
            cfg.maxBodyPairs          = sysDefaults.maxBodyPairs;
            cfg.maxContactConstraints = sysDefaults.maxContactConstraints;
            cfg.tempAllocatorBytes    = sysDefaults.tempAllocatorSizeMB * 1024u * 1024u;
            cfg.gravity               = { sysDefaults.gravity[0], sysDefaults.gravity[1], sysDefaults.gravity[2] };
            cfg.layers                = sysDefaults.layers;
            return cfg;
        }
    };

    struct HitResult {
        BodyID   bodyId     = InvalidBodyID;
        Entity   entity     = Invalid;
        RVec3    position   = {};
        Vec3     normal     = { 0.0f, 1.0f, 0.0f };
        float    fraction   = 1.0f;
        float    distance   = 0.0f;
        uint32_t subShapeId = 0;
    };

    struct ContactEvent {
        BodyID      a           = InvalidBodyID;
        BodyID      b           = InvalidBodyID;
        Entity      entityA     = Invalid;
        Entity      entityB     = Invalid;
        RVec3       point       = {};
        Vec3        normal      = { 0.0f, 1.0f, 0.0f };
        float       penetration = 0.0f;
        ContactType type        = ContactType::Added;
    };

    struct ActiveBodyState {
        BodyID     bodyId          = InvalidBodyID;
        Entity     entity          = Invalid;
        RVec3      position        = {};
        Quat       rotation        = Quat::Identity();
        Vec3       linearVelocity  = { 0.0f, 0.0f, 0.0f };
        Vec3       angularVelocity = { 0.0f, 0.0f, 0.0f };
        MotionType motion          = MotionType::Dynamic;
    };

    // Pimpl-encapsulated PhysicsWorld

    class ZERO_API PhysicsWorld {
    public:
        PhysicsWorld();
        ~PhysicsWorld();

        PhysicsWorld(const PhysicsWorld&)            = delete;
        PhysicsWorld& operator=(const PhysicsWorld&) = delete;
        PhysicsWorld(PhysicsWorld&&) noexcept;
        PhysicsWorld& operator=(PhysicsWorld&&) noexcept;

        // World lifecycle
        bool Init(const PhysicsWorldConfig& config = PhysicsWorldConfig::Default());
        void Shutdown();
        [[nodiscard]] bool IsInitialized() const noexcept;

        void Step(float fixedDt, int collisionSteps = 1);
        void OptimizeBroadPhase();

        void SetGravity(const Vec3& gravity) noexcept;
        [[nodiscard]] Vec3 GetGravity() const noexcept;

        // Shape API
        [[nodiscard]] ShapeID CreateBoxShape(const Vec3& halfExtents, float convexRadius = 0.05f);
        [[nodiscard]] ShapeID CreateSphereShape(float radius);
        [[nodiscard]] ShapeID CreateCapsuleShape(float halfHeightOfCylinder, float radius);
        [[nodiscard]] ShapeID CreateMeshShape(std::span<const Vec3> vertices,
                                              std::span<const uint32_t> indices);
        [[nodiscard]] ShapeID CreateCompoundShape(std::span<const CompoundSubShape> subShapes,
                                                  bool mutableCompound = false);
        void DestroyShape(ShapeID shape);
        [[nodiscard]] bool IsShapeValid(ShapeID shape) const noexcept;

        // Body lifecycle & batch streaming helpers (zero hot-path heap allocations)
        [[nodiscard]] BodyID CreateBody(const BodyDesc& desc, bool addToWorld = true);
        void DestroyBody(BodyID id);

        void AddBody(BodyID id, bool activate = true);
        void RemoveBody(BodyID id);

        // Batch-create and batch-insert into the broadphase (ideal for streaming chunks).
        // Returns the number of bodies successfully created and added.
        uint32_t CreateBodies(std::span<const BodyDesc> descs,
                              std::span<BodyID> outIds,
                              bool activate = true);
        void AddBodies(std::span<const BodyID> ids, bool activate = true);
        void RemoveBodies(std::span<const BodyID> ids);
        void DestroyBodies(std::span<const BodyID> ids);

        // Body state & manipulation
        void SetPositionAndRotation(BodyID id, const RVec3& position, const Quat& rotation, bool activate = true);
        void MoveKinematic(BodyID id, const RVec3& targetPosition, const Quat& targetRotation, float fixedDt);
        void SetLinearVelocity(BodyID id, const Vec3& velocity);
        void SetAngularVelocity(BodyID id, const Vec3& angularVelocity);
        void AddForce(BodyID id, const Vec3& force);
        void AddImpulse(BodyID id, const Vec3& impulse);
        void ActivateBody(BodyID id);

        [[nodiscard]] bool       IsBodyValid(BodyID id) const noexcept;
        [[nodiscard]] bool       IsBodyActive(BodyID id) const noexcept;
        [[nodiscard]] MotionType GetMotionType(BodyID id) const noexcept;
        [[nodiscard]] RVec3      GetPosition(BodyID id) const noexcept;
        [[nodiscard]] Quat       GetRotation(BodyID id) const noexcept;
        [[nodiscard]] Vec3       GetLinearVelocity(BodyID id) const noexcept;
        [[nodiscard]] Vec3       GetAngularVelocity(BodyID id) const noexcept;
        [[nodiscard]] Entity     GetUserData(BodyID id) const noexcept;
        void                     SetUserData(BodyID id, Entity entity) noexcept;

        [[nodiscard]] uint32_t GetBodyCount() const noexcept;
        [[nodiscard]] uint32_t GetActiveBodyCount() const noexcept;

        // Zero-allocation active dynamic body reader.
        // Iterates active bodies starting at `inOutCursor`, writing dynamic body
        // states into `outStates` until `outStates` is full or all active bodies
        // have been scanned. Returns number of states written.
        [[nodiscard]] uint32_t ReadActiveDynamicBodies(uint32_t& inOutCursor,
                                                       std::span<ActiveBodyState> outStates) const noexcept;

        // Zero-allocation spatial queries
        [[nodiscard]] bool Raycast(const RVec3& origin,
                                   const Vec3& dir,
                                   float maxDist,
                                   LayerMask layerMask,
                                   HitResult& outHit) const noexcept;

        [[nodiscard]] uint32_t RaycastAll(const RVec3& origin,
                                          const Vec3& dir,
                                          float maxDist,
                                          LayerMask layerMask,
                                          std::span<HitResult> outHits) const noexcept;

        [[nodiscard]] uint32_t OverlapSphere(const RVec3& center,
                                             float radius,
                                             LayerMask layerMask,
                                             std::span<BodyID> outBodies) const noexcept;

        [[nodiscard]] uint32_t OverlapBox(const RVec3& center,
                                          const Vec3& halfExtents,
                                          const Quat& rotation,
                                          LayerMask layerMask,
                                          std::span<BodyID> outBodies) const noexcept;

        // Zero-allocation frame-scoped contact/trigger events.
        // Step() appends into the buffer so multi-substep frames preserve earlier
        // substeps' collisions for PostPhysics; call ClearContactEvents() once
        // per frame before the FixedStep substep loop.
        [[nodiscard]] std::span<const ContactEvent> GetContactEvents() const noexcept;
        [[nodiscard]] uint32_t GetDroppedContactEventCount() const noexcept;
        void ClearContactEvents() noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };

}
