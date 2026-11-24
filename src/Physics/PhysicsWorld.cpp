#include "pch.h"

#include <Engine/Physics/PhysicsSystemAPI.h>
#include <Engine/Core.h>
#include <Engine/Log.h>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/MutableCompoundShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>

#include "Physics/JoltJobSystemAdapter.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <vector>

namespace Zero
{
    namespace
    {
        // ---------------------------------------------------------------------
        // Reference-counted Jolt global initialization
        // ---------------------------------------------------------------------
        std::mutex g_joltGlobalMutex;
        uint32_t   g_joltRefCount = 0;

        void JoltTraceImpl(const char* inFMT, ...)
        {
            va_list list;
            va_start(list, inFMT);
            char buffer[1024];
            std::vsnprintf(buffer, sizeof(buffer), inFMT, list);
            va_end(list);
            ZERO_CORE_TRACE("[Jolt] {}", buffer);
        }

#ifdef JPH_ENABLE_ASSERTS
        bool JoltAssertFailedImpl(const char* inExpression,
                                  const char* inMessage,
                                  const char* inFile,
                                  JPH::uint inLine)
        {
            ZERO_CORE_ERROR("[Jolt Assert] {}:{}: ({}) {}",
                inFile ? inFile : "",
                inLine,
                inExpression ? inExpression : "",
                inMessage ? inMessage : "");
            return true;
        }
#endif

        void AcquireJoltGlobal()
        {
            std::lock_guard<std::mutex> lock(g_joltGlobalMutex);
            if (g_joltRefCount == 0)
            {
                JPH::RegisterDefaultAllocator();
                JPH::Trace = JoltTraceImpl;
                JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = JoltAssertFailedImpl;)

                JPH::Factory::sInstance = new JPH::Factory();
                JPH::RegisterTypes();
            }
            ++g_joltRefCount;
        }

        void ReleaseJoltGlobal()
        {
            std::lock_guard<std::mutex> lock(g_joltGlobalMutex);
            if (g_joltRefCount == 0)
                return;

            --g_joltRefCount;
            if (g_joltRefCount == 0)
            {
                JPH::UnregisterTypes();
                delete JPH::Factory::sInstance;
                JPH::Factory::sInstance = nullptr;
            }
        }

        // ---------------------------------------------------------------------
        // Math & enum conversions
        // ---------------------------------------------------------------------
        [[nodiscard]] inline JPH::Vec3 ToJolt(const Vec3& v) noexcept
        {
            return JPH::Vec3(v.x, v.y, v.z);
        }

        [[nodiscard]] inline JPH::RVec3 ToJoltR(const Vec3& v) noexcept
        {
            return JPH::RVec3(v.x, v.y, v.z);
        }

        [[nodiscard]] inline JPH::Quat ToJolt(const Quat& q) noexcept
        {
            const Quat n = q.Normalized();
            return JPH::Quat(n.x, n.y, n.z, n.w);
        }

        [[nodiscard]] inline Vec3 ToZero(const JPH::Vec3& v) noexcept
        {
            return Vec3{ v.GetX(), v.GetY(), v.GetZ() };
        }

#ifdef JPH_DOUBLE_PRECISION
        [[nodiscard]] inline Vec3 ToZero(const JPH::RVec3& v) noexcept
        {
            return Vec3{
                static_cast<float>(v.GetX()),
                static_cast<float>(v.GetY()),
                static_cast<float>(v.GetZ())
            };
        }
#endif

        [[nodiscard]] inline Quat ToZero(const JPH::Quat& q) noexcept
        {
            return Quat{ q.GetX(), q.GetY(), q.GetZ(), q.GetW() };
        }

        [[nodiscard]] inline JPH::BodyID ToJolt(BodyID id) noexcept
        {
            return JPH::BodyID(id.value);
        }

        [[nodiscard]] inline BodyID ToZero(const JPH::BodyID& id) noexcept
        {
            return BodyID{ id.GetIndexAndSequenceNumber() };
        }

        [[nodiscard]] inline JPH::EMotionType ToJolt(MotionType m) noexcept
        {
            switch (m)
            {
            case MotionType::Static:    return JPH::EMotionType::Static;
            case MotionType::Kinematic: return JPH::EMotionType::Kinematic;
            case MotionType::Dynamic:   return JPH::EMotionType::Dynamic;
            }
            return JPH::EMotionType::Dynamic;
        }

        [[nodiscard]] inline MotionType ToZero(JPH::EMotionType m) noexcept
        {
            switch (m)
            {
            case JPH::EMotionType::Static:    return MotionType::Static;
            case JPH::EMotionType::Kinematic: return MotionType::Kinematic;
            case JPH::EMotionType::Dynamic:   return MotionType::Dynamic;
            }
            return MotionType::Dynamic;
        }

        [[nodiscard]] inline JPH::ObjectLayer ToJoltLayer(PhysicsLayer layer) noexcept
        {
            const uint8_t idx = static_cast<uint8_t>(layer);
            if (idx < kMaxObjectLayers)
                return static_cast<JPH::ObjectLayer>(idx);
            return static_cast<JPH::ObjectLayer>(ObjectLayer::DYNAMIC);
        }

        // ---------------------------------------------------------------------
        // Broadphase & Object Layer Filters
        // ---------------------------------------------------------------------
        namespace BPLayers
        {
            inline constexpr JPH::BroadPhaseLayer NON_MOVING(0);
            inline constexpr JPH::BroadPhaseLayer MOVING(1);
            inline constexpr JPH::BroadPhaseLayer TRIGGER(2);
            inline constexpr JPH::uint            NUM_LAYERS = 3;
        }

        [[nodiscard]] constexpr LayerMask BroadPhaseBucketMask(JPH::BroadPhaseLayer bpLayer) noexcept
        {
            if (bpLayer == BPLayers::NON_MOVING)
                return MakeMask(ObjectLayer::STATIC);
            if (bpLayer == BPLayers::TRIGGER)
                return MakeMask(ObjectLayer::TRIGGER);
            return MakeMask(ObjectLayer::DYNAMIC,
                            ObjectLayer::KINEMATIC,
                            ObjectLayer::PROJECTILE,
                            ObjectLayer::RAGDOLL);
        }

        class ZeroBPLayerInterface final : public JPH::BroadPhaseLayerInterface
        {
        public:
            ZeroBPLayerInterface()
            {
                m_objectToBP[static_cast<size_t>(ObjectLayer::STATIC)]     = BPLayers::NON_MOVING;
                m_objectToBP[static_cast<size_t>(ObjectLayer::DYNAMIC)]    = BPLayers::MOVING;
                m_objectToBP[static_cast<size_t>(ObjectLayer::KINEMATIC)]  = BPLayers::MOVING;
                m_objectToBP[static_cast<size_t>(ObjectLayer::PROJECTILE)] = BPLayers::MOVING;
                m_objectToBP[static_cast<size_t>(ObjectLayer::TRIGGER)]    = BPLayers::TRIGGER;
                m_objectToBP[static_cast<size_t>(ObjectLayer::RAGDOLL)]    = BPLayers::MOVING;
            }

            JPH::uint GetNumBroadPhaseLayers() const override
            {
                return BPLayers::NUM_LAYERS;
            }

            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
            {
                const size_t idx = static_cast<size_t>(inLayer);
                if (idx < kMaxObjectLayers)
                    return m_objectToBP[idx];
                return BPLayers::MOVING;
            }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
            {
                switch (static_cast<JPH::BroadPhaseLayer::Type>(inLayer))
                {
                case 0:  return "NON_MOVING";
                case 1:  return "MOVING";
                case 2:  return "TRIGGER";
                default: return "UNKNOWN";
                }
            }
#endif

        private:
            std::array<JPH::BroadPhaseLayer, kMaxObjectLayers> m_objectToBP{};
        };

        class ZeroObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter
        {
        public:
            void Configure(const std::array<LayerConfig, kMaxObjectLayers>& layers) noexcept
            {
                for (size_t i = 0; i < kMaxObjectLayers; ++i)
                    m_collisionMasks[i] = layers[i].collidesWith;
            }

            bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
            {
                const size_t i1 = static_cast<size_t>(inObject1);
                const size_t i2 = static_cast<size_t>(inObject2);
                if (i1 >= kMaxObjectLayers || i2 >= kMaxObjectLayers)
                    return false;

                return (m_collisionMasks[i1] & (1u << i2)) != 0 ||
                       (m_collisionMasks[i2] & (1u << i1)) != 0;
            }

        private:
            std::array<LayerMask, kMaxObjectLayers> m_collisionMasks{};
        };

        class ZeroObjectVsBPLayerFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
        {
        public:
            void Configure(const std::array<LayerConfig, kMaxObjectLayers>& layers) noexcept
            {
                for (size_t i = 0; i < kMaxObjectLayers; ++i)
                    m_collisionMasks[i] = layers[i].collidesWith;
            }

            bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
            {
                const size_t idx = static_cast<size_t>(inLayer1);
                if (idx >= kMaxObjectLayers)
                    return false;

                return (m_collisionMasks[idx] & BroadPhaseBucketMask(inLayer2)) != 0;
            }

        private:
            std::array<LayerMask, kMaxObjectLayers> m_collisionMasks{};
        };

        // Spatial query filters (stack-allocated, zero heap allocations)
        class LayerMaskBPFilter final : public JPH::BroadPhaseLayerFilter
        {
        public:
            explicit LayerMaskBPFilter(LayerMask mask) noexcept : m_mask(mask) {}

            bool ShouldCollide(JPH::BroadPhaseLayer inLayer) const override
            {
                return (m_mask & BroadPhaseBucketMask(inLayer)) != 0;
            }

        private:
            LayerMask m_mask;
        };

        class LayerMaskObjectFilter final : public JPH::ObjectLayerFilter
        {
        public:
            explicit LayerMaskObjectFilter(LayerMask mask) noexcept : m_mask(mask) {}

            bool ShouldCollide(JPH::ObjectLayer inLayer) const override
            {
                const uint32_t idx = static_cast<uint32_t>(inLayer);
                if (idx >= kMaxObjectLayers)
                    return false;
                return (m_mask & (1u << idx)) != 0;
            }

        private:
            LayerMask m_mask;
        };

        // ---------------------------------------------------------------------
        // Generational ShapeID storage
        // ---------------------------------------------------------------------
        constexpr uint32_t kShapeIndexBits = 20;
        constexpr uint32_t kShapeIndexMask = (1u << kShapeIndexBits) - 1u;
        constexpr uint32_t kShapeGenMask   = 0xFFFu; // 12 bits; 0xFFF reserved with UINT32_MAX

        struct ShapeSlot
        {
            JPH::RefConst<JPH::Shape> shape;
            uint32_t                  generation = 1;
            uint32_t                  nextFree   = UINT32_MAX;
        };

        [[nodiscard]] constexpr ShapeID PackShapeID(uint32_t idx, uint32_t gen) noexcept
        {
            return ShapeID{ ((gen & kShapeGenMask) << kShapeIndexBits) | (idx & kShapeIndexMask) };
        }

        [[nodiscard]] constexpr uint32_t ShapeSlotIndex(ShapeID id) noexcept
        {
            return id.value & kShapeIndexMask;
        }

        [[nodiscard]] constexpr uint32_t ShapeSlotGen(ShapeID id) noexcept
        {
            return (id.value >> kShapeIndexBits) & kShapeGenMask;
        }

        // Fast body index -> Entity & sensor cache for O(1) ContactRemoved resolution
        // without touching body locks during callbacks.
        struct BodyEntityEntry
        {
            uint32_t bodyIdRaw = UINT32_MAX;
            Entity   entity    = Invalid;
            bool     isSensor  = false;
        };

        // ---------------------------------------------------------------------
        // Lock-free atomic-indexed ContactListener
        // ---------------------------------------------------------------------
        class ZeroContactListener final : public JPH::ContactListener
        {
        public:
            void Reserve(uint32_t capacity, bool assertOnOverflow = true)
            {
                m_events.resize(capacity);
                m_assertOnOverflow = assertOnOverflow;
                m_writeIndex.store(0, std::memory_order_relaxed);
                m_dropped.store(0, std::memory_order_relaxed);
                m_committedCount = 0;
            }

            void EndStep() noexcept
            {
                const uint32_t written = m_writeIndex.load(std::memory_order_acquire);
                m_committedCount = std::min<uint32_t>(written, static_cast<uint32_t>(m_events.size()));
            }

            void Clear() noexcept
            {
                m_writeIndex.store(0, std::memory_order_relaxed);
                m_dropped.store(0, std::memory_order_relaxed);
                m_committedCount = 0;
            }

            void SetBodyEntityCache(const std::vector<BodyEntityEntry>* cache) noexcept
            {
                m_bodyCache = cache;
            }

            [[nodiscard]] std::span<const ContactEvent> Events() const noexcept
            {
                return { m_events.data(), m_committedCount };
            }

            [[nodiscard]] uint32_t DroppedCount() const noexcept
            {
                return m_dropped.load(std::memory_order_relaxed);
            }

            void OnContactAdded(const JPH::Body& inBody1,
                                const JPH::Body& inBody2,
                                const JPH::ContactManifold& inManifold,
                                JPH::ContactSettings& /*ioSettings*/) override
            {
                const bool isTrigger = inBody1.IsSensor() || inBody2.IsSensor();
                RecordManifoldEvent(inBody1, inBody2, inManifold,
                    isTrigger ? ContactType::TriggerEnter : ContactType::Added);
            }

            void OnContactPersisted(const JPH::Body& inBody1,
                                    const JPH::Body& inBody2,
                                    const JPH::ContactManifold& inManifold,
                                    JPH::ContactSettings& /*ioSettings*/) override
            {
                RecordManifoldEvent(inBody1, inBody2, inManifold, ContactType::Persisted);
            }

            void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override
            {
                ContactEvent ev;
                ev.a = ToZero(inSubShapePair.GetBody1ID());
                ev.b = ToZero(inSubShapePair.GetBody2ID());

                bool isTrigger = false;
                if (m_bodyCache)
                {
                    const uint32_t idx1 = inSubShapePair.GetBody1ID().GetIndex();
                    const uint32_t idx2 = inSubShapePair.GetBody2ID().GetIndex();
                    if (idx1 < m_bodyCache->size())
                    {
                        const BodyEntityEntry& e1 = (*m_bodyCache)[idx1];
                        if (e1.bodyIdRaw == ev.a.value)
                        {
                            ev.entityA = e1.entity;
                            isTrigger = isTrigger || e1.isSensor;
                        }
                    }
                    if (idx2 < m_bodyCache->size())
                    {
                        const BodyEntityEntry& e2 = (*m_bodyCache)[idx2];
                        if (e2.bodyIdRaw == ev.b.value)
                        {
                            ev.entityB = e2.entity;
                            isTrigger = isTrigger || e2.isSensor;
                        }
                    }
                }

                ev.type = isTrigger ? ContactType::TriggerExit : ContactType::Removed;
                PushEvent(ev);
            }

        private:
            void RecordManifoldEvent(const JPH::Body& inBody1,
                                     const JPH::Body& inBody2,
                                     const JPH::ContactManifold& inManifold,
                                     ContactType type) noexcept
            {
                ContactEvent ev;
                ev.a           = ToZero(inBody1.GetID());
                ev.b           = ToZero(inBody2.GetID());
                ev.entityA     = static_cast<Entity>(inBody1.GetUserData());
                ev.entityB     = static_cast<Entity>(inBody2.GetUserData());
                ev.normal      = ToZero(inManifold.mWorldSpaceNormal);
                ev.penetration = inManifold.mPenetrationDepth;
                ev.type        = type;

                if (!inManifold.mRelativeContactPointsOn1.empty())
                    ev.point = ToZero(inManifold.GetWorldSpaceContactPointOn1(0));
                else
                    ev.point = ToZero(inManifold.mBaseOffset);

                PushEvent(ev);
            }

            void PushEvent(const ContactEvent& ev) noexcept
            {
                const uint32_t idx = m_writeIndex.fetch_add(1, std::memory_order_relaxed);
                if (idx >= static_cast<uint32_t>(m_events.size()))
                {
                    m_dropped.fetch_add(1, std::memory_order_relaxed);
                    if (m_assertOnOverflow)
                    {
                        ZERO_CORE_ASSERT(false, "PhysicsWorld ContactEvent buffer overflow");
                    }
                    return;
                }
                m_events[idx] = ev;
            }

            std::vector<ContactEvent>           m_events;
            std::atomic<uint32_t>               m_writeIndex{ 0 };
            std::atomic<uint32_t>               m_dropped{ 0 };
            uint32_t                            m_committedCount   = 0;
            bool                                m_assertOnOverflow = true;
            const std::vector<BodyEntityEntry>* m_bodyCache        = nullptr;
        };

        // ---------------------------------------------------------------------
        // Zero-allocation Spatial Query Collectors
        // ---------------------------------------------------------------------
        class ZeroCastRayAllCollector final : public JPH::CastRayCollector
        {
        public:
            explicit ZeroCastRayAllCollector(std::span<HitResult> outHits) noexcept
                : m_hits(outHits)
            {
            }

            void AddHit(const JPH::RayCastResult& inResult) override
            {
                const uint32_t cap = static_cast<uint32_t>(m_hits.size());
                if (cap == 0)
                {
                    ForceEarlyOut();
                    return;
                }

                if (m_count < cap)
                {
                    HitResult& dst = m_hits[m_count++];
                    dst.bodyId     = ToZero(inResult.mBodyID);
                    dst.fraction   = inResult.mFraction;
                    dst.subShapeId = inResult.mSubShapeID2.GetValue();

                    if (m_count == cap)
                    {
                        float maxFrac = 0.0f;
                        for (uint32_t i = 0; i < cap; ++i)
                            maxFrac = std::max(maxFrac, m_hits[i].fraction);
                        UpdateEarlyOutFraction(maxFrac);
                    }
                    return;
                }

                // Buffer full: replace furthest hit if this hit is closer
                uint32_t maxIdx  = 0;
                float    maxFrac = m_hits[0].fraction;
                for (uint32_t i = 1; i < cap; ++i)
                {
                    if (m_hits[i].fraction > maxFrac)
                    {
                        maxFrac = m_hits[i].fraction;
                        maxIdx  = i;
                    }
                }

                if (inResult.mFraction < maxFrac)
                {
                    m_hits[maxIdx].bodyId     = ToZero(inResult.mBodyID);
                    m_hits[maxIdx].fraction   = inResult.mFraction;
                    m_hits[maxIdx].subShapeId = inResult.mSubShapeID2.GetValue();

                    float newMax = 0.0f;
                    for (uint32_t i = 0; i < cap; ++i)
                        newMax = std::max(newMax, m_hits[i].fraction);
                    UpdateEarlyOutFraction(newMax);
                }
            }

            [[nodiscard]] uint32_t Count() const noexcept { return m_count; }

        private:
            std::span<HitResult> m_hits;
            uint32_t             m_count = 0;
        };

        class ZeroOverlapCollector final : public JPH::CollideShapeCollector
        {
        public:
            explicit ZeroOverlapCollector(std::span<BodyID> outBodies) noexcept
                : m_out(outBodies)
            {
            }

            void AddHit(const JPH::CollideShapeResult& inResult) override
            {
                const BodyID id = ToZero(inResult.mBodyID2);
                for (uint32_t i = 0; i < m_count; ++i)
                {
                    if (m_out[i] == id)
                        return;
                }

                if (m_count < static_cast<uint32_t>(m_out.size()))
                {
                    m_out[m_count++] = id;
                    if (m_count >= static_cast<uint32_t>(m_out.size()))
                        ForceEarlyOut();
                }
                else
                {
                    ForceEarlyOut();
                }
            }

            [[nodiscard]] uint32_t Count() const noexcept { return m_count; }

        private:
            std::span<BodyID> m_out;
            uint32_t          m_count = 0;
        };
    } // anonymous namespace

    // =========================================================================
    // PhysicsWorld::Impl
    // =========================================================================

    struct PhysicsWorld::Impl
    {
        bool initialized = false;
        bool joltAcquired = false;

        PhysicsWorldConfig config{};

        ZeroBPLayerInterface        bpInterface;
        ZeroObjectVsBPLayerFilter   objVsBpFilter;
        ZeroObjectLayerPairFilter   objPairFilter;
        ZeroContactListener         contactListener;

        std::unique_ptr<JPH::TempAllocatorImpl> tempAllocator;
        std::unique_ptr<JoltJobSystemAdapter>   jobSystem;
        JPH::PhysicsSystem                      physicsSystem;

        // Pre-allocated shape slot table (generational)
        std::vector<ShapeSlot>                  shapes;
        uint32_t                                freeShapeHead = UINT32_MAX;

        // Cached default 1x1x1 unit box shape (half-extents 0.5)
        JPH::RefConst<JPH::Shape>               defaultBoxShape;

        // Pre-allocated body index -> Entity/Sensor cache
        std::vector<BodyEntityEntry>            bodyEntityCache;

        ~Impl()
        {
            Shutdown();
        }

        bool Init(const PhysicsWorldConfig& cfg)
        {
            if (initialized)
                Shutdown();

            AcquireJoltGlobal();
            joltAcquired = true;

            config = cfg;
            if (config.maxBodies == 0) config.maxBodies = 1024;
            if (config.maxBodyPairs == 0) config.maxBodyPairs = 1024;
            if (config.maxContactConstraints == 0) config.maxContactConstraints = 1024;
            if (config.tempAllocatorBytes == 0) config.tempAllocatorBytes = 4u * 1024u * 1024u;
            if (config.maxContactEvents == 0) config.maxContactEvents = 1024;
            if (config.maxJoltJobs == 0) config.maxJoltJobs = 2048;
            if (config.maxJoltBarriers == 0) config.maxJoltBarriers = 16;

            objPairFilter.Configure(config.layers);
            objVsBpFilter.Configure(config.layers);

            tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(config.tempAllocatorBytes);
            jobSystem     = std::make_unique<JoltJobSystemAdapter>(
                BlockingThreadPool::Get(),
                config.maxJoltJobs,
                config.maxJoltBarriers);

            physicsSystem.Init(
                config.maxBodies,
                config.numBodyMutexes,
                config.maxBodyPairs,
                config.maxContactConstraints,
                bpInterface,
                objVsBpFilter,
                objPairFilter);

            physicsSystem.SetGravity(ToJolt(config.gravity));

            bodyEntityCache.assign(config.maxBodies, BodyEntityEntry{});
            contactListener.Reserve(config.maxContactEvents, config.assertOnContactOverflow);
            contactListener.SetBodyEntityCache(&bodyEntityCache);
            physicsSystem.SetContactListener(&contactListener);

            shapes.clear();
            shapes.reserve(config.maxShapes);
            freeShapeHead = UINT32_MAX;

            // Pre-build a reusable default unit box shape
            {
                JPH::BoxShapeSettings boxSettings(JPH::Vec3(0.5f, 0.5f, 0.5f), 0.05f);
                JPH::ShapeSettings::ShapeResult res = boxSettings.Create();
                if (res.IsValid())
                    defaultBoxShape = res.Get();
            }

            initialized = true;
            return true;
        }

        void Shutdown()
        {
            if (!initialized && !joltAcquired)
                return;

            if (initialized)
            {
                physicsSystem.SetContactListener(nullptr);

                // Destroy any remaining bodies in Jolt
                const uint32_t bodyCount = physicsSystem.GetNumBodies();
                if (bodyCount > 0)
                {
                    JPH::BodyIDVector allBodies;
                    physicsSystem.GetBodies(allBodies);
                    if (!allBodies.empty())
                    {
                        auto& bi = physicsSystem.GetBodyInterfaceNoLock();
                        constexpr size_t kBatch = 128;
                        JPH::BodyID toRemove[kBatch];
                        size_t i = 0;
                        while (i < allBodies.size())
                        {
                            size_t remCount = 0;
                            const size_t end = std::min(allBodies.size(), i + kBatch);
                            for (size_t k = i; k < end; ++k)
                            {
                                if (bi.IsAdded(allBodies[k]))
                                    toRemove[remCount++] = allBodies[k];
                            }
                            if (remCount > 0)
                                bi.RemoveBodies(toRemove, static_cast<int>(remCount));
                            bi.DestroyBodies(allBodies.data() + i, static_cast<int>(end - i));
                            i = end;
                        }
                    }
                }

                defaultBoxShape = nullptr;
                shapes.clear();
                freeShapeHead = UINT32_MAX;
                bodyEntityCache.clear();
                contactListener.SetBodyEntityCache(nullptr);
                contactListener.Clear();

                jobSystem.reset();
                tempAllocator.reset();
                initialized = false;
            }

            if (joltAcquired)
            {
                ReleaseJoltGlobal();
                joltAcquired = false;
            }
        }

        ShapeID RegisterShape(JPH::RefConst<JPH::Shape> shape)
        {
            if (!shape)
                return InvalidShapeID;

            if (freeShapeHead != UINT32_MAX)
            {
                const uint32_t idx = freeShapeHead;
                ShapeSlot& slot = shapes[idx];
                freeShapeHead = slot.nextFree;
                slot.nextFree = UINT32_MAX;
                slot.shape    = std::move(shape);
                return PackShapeID(idx, slot.generation);
            }

            if (shapes.size() >= kShapeIndexMask)
                return InvalidShapeID;

            const uint32_t idx = static_cast<uint32_t>(shapes.size());
            shapes.push_back(ShapeSlot{ std::move(shape), 1u, UINT32_MAX });
            return PackShapeID(idx, 1u);
        }

        const JPH::Shape* ResolveShape(ShapeID id) const noexcept
        {
            if (id.IsInvalid())
                return nullptr;

            const uint32_t idx = ShapeSlotIndex(id);
            const uint32_t gen = ShapeSlotGen(id);
            if (idx >= shapes.size())
                return nullptr;

            const ShapeSlot& slot = shapes[idx];
            if (slot.generation != gen || !slot.shape)
                return nullptr;

            return slot.shape.GetPtr();
        }

        JPH::RefConst<JPH::Shape> ResolveOrCreateFallbackShape(const BodyDesc& desc) const
        {
            if (const JPH::Shape* s = ResolveShape(desc.shape))
                return JPH::RefConst<JPH::Shape>(s);

            const float hx = std::max(std::fabs(desc.fallbackHalfExtents.x * desc.scale.x), 1e-3f);
            const float hy = std::max(std::fabs(desc.fallbackHalfExtents.y * desc.scale.y), 1e-3f);
            const float hz = std::max(std::fabs(desc.fallbackHalfExtents.z * desc.scale.z), 1e-3f);

            if (std::fabs(hx - 0.5f) < 1e-5f &&
                std::fabs(hy - 0.5f) < 1e-5f &&
                std::fabs(hz - 0.5f) < 1e-5f &&
                defaultBoxShape)
            {
                return defaultBoxShape;
            }

            const float minExt = std::min({ hx, hy, hz });
            const float convex = std::min(0.05f, minExt * 0.5f);
            JPH::BoxShapeSettings settings(JPH::Vec3(hx, hy, hz), convex);
            JPH::ShapeSettings::ShapeResult res = settings.Create();
            return res.IsValid() ? res.Get() : defaultBoxShape;
        }

        bool BuildCreationSettings(const BodyDesc& desc, JPH::BodyCreationSettings& outSettings) const
        {
            JPH::RefConst<JPH::Shape> shape = ResolveOrCreateFallbackShape(desc);
            if (!shape)
                return false;

            outSettings = JPH::BodyCreationSettings(
                shape.GetPtr(),
                ToJoltR(desc.position),
                ToJolt(desc.rotation),
                ToJolt(desc.motion),
                ToJoltLayer(desc.layer));

            outSettings.mRestitution    = desc.restitution;
            outSettings.mFriction       = desc.friction;
            outSettings.mLinearDamping  = desc.linearDamping;
            outSettings.mAngularDamping = desc.angularDamping;
            outSettings.mGravityFactor  = desc.gravityFactor;
            outSettings.mIsSensor       = desc.isSensor;
            outSettings.mAllowSleeping  = desc.allowSleeping;
            outSettings.mUserData       = static_cast<JPH::uint64>(desc.userData);
            outSettings.mLinearVelocity = ToJolt(desc.linearVelocity);
            outSettings.mAngularVelocity = ToJolt(desc.angularVelocity);

            if (desc.motion == MotionType::Dynamic && desc.mass > 0.0f)
            {
                outSettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                outSettings.mMassPropertiesOverride.mMass = desc.mass;
            }

            return true;
        }

        void RecordBodyCache(const JPH::BodyID& jphId, Entity userData, bool isSensor) noexcept
        {
            const uint32_t idx = jphId.GetIndex();
            if (idx < bodyEntityCache.size())
            {
                bodyEntityCache[idx].bodyIdRaw = jphId.GetIndexAndSequenceNumber();
                bodyEntityCache[idx].entity    = userData;
                bodyEntityCache[idx].isSensor  = isSensor;
            }
        }

        void ClearBodyCache(const JPH::BodyID& jphId) noexcept
        {
            const uint32_t idx = jphId.GetIndex();
            if (idx < bodyEntityCache.size() &&
                bodyEntityCache[idx].bodyIdRaw == jphId.GetIndexAndSequenceNumber())
            {
                bodyEntityCache[idx] = BodyEntityEntry{};
            }
        }
    };

    // =========================================================================
    // PhysicsWorld Public API
    // =========================================================================

    PhysicsWorld::PhysicsWorld()
        : m_impl(std::make_unique<Impl>())
    {
    }

    PhysicsWorld::~PhysicsWorld() = default;

    PhysicsWorld::PhysicsWorld(PhysicsWorld&&) noexcept = default;
    PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&&) noexcept = default;

    bool PhysicsWorld::Init(const PhysicsWorldConfig& config)
    {
        if (!m_impl)
            m_impl = std::make_unique<Impl>();
        return m_impl->Init(config);
    }

    void PhysicsWorld::Shutdown()
    {
        if (m_impl)
            m_impl->Shutdown();
    }

    bool PhysicsWorld::IsInitialized() const noexcept
    {
        return m_impl && m_impl->initialized;
    }

    void PhysicsWorld::Step(float fixedDt, int collisionSteps)
    {
        if (!IsInitialized() || fixedDt <= 0.0f || collisionSteps <= 0)
            return;

        m_impl->physicsSystem.Update(
            fixedDt,
            collisionSteps,
            m_impl->tempAllocator.get(),
            m_impl->jobSystem.get());
        m_impl->contactListener.EndStep();
    }

    void PhysicsWorld::OptimizeBroadPhase()
    {
        if (!IsInitialized())
            return;
        m_impl->physicsSystem.OptimizeBroadPhase();
    }

    void PhysicsWorld::SetGravity(const Vec3& gravity) noexcept
    {
        if (!IsInitialized())
            return;
        m_impl->config.gravity = gravity;
        m_impl->physicsSystem.SetGravity(ToJolt(gravity));
    }

    Vec3 PhysicsWorld::GetGravity() const noexcept
    {
        if (!IsInitialized())
            return Vec3{ 0.0f, -9.81f, 0.0f };
        return ToZero(m_impl->physicsSystem.GetGravity());
    }

    // -------------------------------------------------------------------------
    // Shape API
    // -------------------------------------------------------------------------

    ShapeID PhysicsWorld::CreateBoxShape(const Vec3& halfExtents, float convexRadius)
    {
        if (!IsInitialized())
            return InvalidShapeID;

        const float hx = std::max(std::fabs(halfExtents.x), 1e-3f);
        const float hy = std::max(std::fabs(halfExtents.y), 1e-3f);
        const float hz = std::max(std::fabs(halfExtents.z), 1e-3f);
        const float minExt = std::min({ hx, hy, hz });
        const float clampedRadius = std::clamp(convexRadius, 0.0f, minExt * 0.5f);

        JPH::BoxShapeSettings settings(JPH::Vec3(hx, hy, hz), clampedRadius);
        JPH::ShapeSettings::ShapeResult res = settings.Create();
        if (!res.IsValid())
        {
            ZERO_CORE_ERROR("CreateBoxShape failed: {}", res.GetError().c_str());
            return InvalidShapeID;
        }
        return m_impl->RegisterShape(res.Get());
    }

    ShapeID PhysicsWorld::CreateSphereShape(float radius)
    {
        if (!IsInitialized())
            return InvalidShapeID;

        const float r = std::max(radius, 1e-3f);
        JPH::SphereShapeSettings settings(r);
        JPH::ShapeSettings::ShapeResult res = settings.Create();
        if (!res.IsValid())
        {
            ZERO_CORE_ERROR("CreateSphereShape failed: {}", res.GetError().c_str());
            return InvalidShapeID;
        }
        return m_impl->RegisterShape(res.Get());
    }

    ShapeID PhysicsWorld::CreateCapsuleShape(float halfHeightOfCylinder, float radius)
    {
        if (!IsInitialized())
            return InvalidShapeID;

        const float hh = std::max(halfHeightOfCylinder, 1e-3f);
        const float r  = std::max(radius, 1e-3f);
        JPH::CapsuleShapeSettings settings(hh, r);
        JPH::ShapeSettings::ShapeResult res = settings.Create();
        if (!res.IsValid())
        {
            ZERO_CORE_ERROR("CreateCapsuleShape failed: {}", res.GetError().c_str());
            return InvalidShapeID;
        }
        return m_impl->RegisterShape(res.Get());
    }

    ShapeID PhysicsWorld::CreateMeshShape(std::span<const Vec3> vertices,
                                          std::span<const uint32_t> indices)
    {
        if (!IsInitialized() || vertices.empty() || indices.size() < 3 || (indices.size() % 3) != 0)
            return InvalidShapeID;

        JPH::VertexList jphVerts;
        jphVerts.reserve(vertices.size());
        for (const Vec3& v : vertices)
            jphVerts.emplace_back(v.x, v.y, v.z);

        JPH::IndexedTriangleList jphTris;
        const size_t triCount = indices.size() / 3;
        jphTris.reserve(triCount);
        for (size_t i = 0; i < triCount; ++i)
        {
            const uint32_t i0 = indices[i * 3 + 0];
            const uint32_t i1 = indices[i * 3 + 1];
            const uint32_t i2 = indices[i * 3 + 2];
            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
                return InvalidShapeID;
            jphTris.emplace_back(i0, i1, i2);
        }

        JPH::MeshShapeSettings settings(std::move(jphVerts), std::move(jphTris));
        JPH::ShapeSettings::ShapeResult res = settings.Create();
        if (!res.IsValid())
        {
            ZERO_CORE_ERROR("CreateMeshShape failed: {}", res.GetError().c_str());
            return InvalidShapeID;
        }
        return m_impl->RegisterShape(res.Get());
    }

    ShapeID PhysicsWorld::CreateCompoundShape(std::span<const CompoundSubShape> subShapes,
                                              bool mutableCompound)
    {
        if (!IsInitialized() || subShapes.empty())
            return InvalidShapeID;

        if (mutableCompound)
        {
            JPH::MutableCompoundShapeSettings settings;
            for (const auto& sub : subShapes)
            {
                const JPH::Shape* s = m_impl->ResolveShape(sub.shape);
                if (!s)
                    return InvalidShapeID;
                settings.AddShape(ToJolt(sub.position), ToJolt(sub.rotation), s);
            }
            JPH::ShapeSettings::ShapeResult res = settings.Create();
            if (!res.IsValid())
            {
                ZERO_CORE_ERROR("CreateCompoundShape (mutable) failed: {}", res.GetError().c_str());
                return InvalidShapeID;
            }
            return m_impl->RegisterShape(res.Get());
        }
        else
        {
            JPH::StaticCompoundShapeSettings settings;
            for (const auto& sub : subShapes)
            {
                const JPH::Shape* s = m_impl->ResolveShape(sub.shape);
                if (!s)
                    return InvalidShapeID;
                settings.AddShape(ToJolt(sub.position), ToJolt(sub.rotation), s);
            }
            JPH::ShapeSettings::ShapeResult res = settings.Create();
            if (!res.IsValid())
            {
                ZERO_CORE_ERROR("CreateCompoundShape (static) failed: {}", res.GetError().c_str());
                return InvalidShapeID;
            }
            return m_impl->RegisterShape(res.Get());
        }
    }

    void PhysicsWorld::DestroyShape(ShapeID shape)
    {
        if (!IsInitialized() || shape.IsInvalid())
            return;

        const uint32_t idx = ShapeSlotIndex(shape);
        const uint32_t gen = ShapeSlotGen(shape);
        if (idx >= m_impl->shapes.size())
            return;

        ShapeSlot& slot = m_impl->shapes[idx];
        if (slot.generation != gen || !slot.shape)
            return;

        slot.shape = nullptr;
        ++slot.generation;
        if (slot.generation == 0 || slot.generation >= kShapeGenMask)
            slot.generation = 1;
        slot.nextFree         = m_impl->freeShapeHead;
        m_impl->freeShapeHead = idx;
    }

    bool PhysicsWorld::IsShapeValid(ShapeID shape) const noexcept
    {
        return IsInitialized() && m_impl->ResolveShape(shape) != nullptr;
    }

    // -------------------------------------------------------------------------
    // Body Lifecycle & Batch Helpers
    // -------------------------------------------------------------------------

    BodyID PhysicsWorld::CreateBody(const BodyDesc& desc, bool addToWorld)
    {
        if (!IsInitialized())
            return InvalidBodyID;

        JPH::BodyCreationSettings settings;
        if (!m_impl->BuildCreationSettings(desc, settings))
            return InvalidBodyID;

        JPH::BodyInterface& bi = m_impl->physicsSystem.GetBodyInterface();
        JPH::Body* body = bi.CreateBody(settings);
        if (!body)
            return InvalidBodyID;

        const JPH::BodyID jphId = body->GetID();
        m_impl->RecordBodyCache(jphId, desc.userData, desc.isSensor);

        if (addToWorld)
        {
            const JPH::EActivation act =
                (desc.startActivated && desc.motion != MotionType::Static)
                    ? JPH::EActivation::Activate
                    : JPH::EActivation::DontActivate;
            bi.AddBody(jphId, act);
        }

        return ToZero(jphId);
    }

    void PhysicsWorld::DestroyBody(BodyID id)
    {
        if (!IsInitialized() || id.IsInvalid())
            return;

        const JPH::BodyID jphId = ToJolt(id);
        if (!IsBodyValid(id))
            return;

        JPH::BodyInterface& bi = m_impl->physicsSystem.GetBodyInterface();
        if (bi.IsAdded(jphId))
            bi.RemoveBody(jphId);

        m_impl->ClearBodyCache(jphId);
        bi.DestroyBody(jphId);
    }

    void PhysicsWorld::AddBody(BodyID id, bool activate)
    {
        if (!IsInitialized() || id.IsInvalid())
            return;

        const JPH::BodyID jphId = ToJolt(id);
        JPH::BodyInterface& bi  = m_impl->physicsSystem.GetBodyInterface();
        if (!bi.IsAdded(jphId))
        {
            bi.AddBody(jphId, activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
        }
    }

    void PhysicsWorld::RemoveBody(BodyID id)
    {
        if (!IsInitialized() || id.IsInvalid())
            return;

        const JPH::BodyID jphId = ToJolt(id);
        JPH::BodyInterface& bi  = m_impl->physicsSystem.GetBodyInterface();
        if (bi.IsAdded(jphId))
            bi.RemoveBody(jphId);
    }

    uint32_t PhysicsWorld::CreateBodies(std::span<const BodyDesc> descs,
                                        std::span<BodyID> outIds,
                                        bool activate)
    {
        if (!IsInitialized() || descs.empty() || outIds.size() < descs.size())
            return 0;

        constexpr size_t kBatchSize = 128;
        JPH::BodyID jphBatch[kBatchSize];

        JPH::BodyInterface& bi = m_impl->physicsSystem.GetBodyInterface();
        uint32_t totalCreated  = 0;

        size_t offset = 0;
        while (offset < descs.size())
        {
            const size_t chunkEnd = std::min(descs.size(), offset + kBatchSize);
            int validCount = 0;

            for (size_t i = offset; i < chunkEnd; ++i)
            {
                outIds[i] = InvalidBodyID;

                JPH::BodyCreationSettings settings;
                if (!m_impl->BuildCreationSettings(descs[i], settings))
                    continue;

                JPH::Body* body = bi.CreateBody(settings);
                if (!body)
                    continue;

                const JPH::BodyID jphId = body->GetID();
                m_impl->RecordBodyCache(jphId, descs[i].userData, descs[i].isSensor);

                outIds[i]              = ToZero(jphId);
                jphBatch[validCount++] = jphId;
            }

            if (validCount > 0)
            {
                const JPH::BroadPhase::AddState addState = bi.AddBodiesPrepare(jphBatch, validCount);
                bi.AddBodiesFinalize(
                    jphBatch,
                    validCount,
                    addState,
                    activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
                totalCreated += static_cast<uint32_t>(validCount);
            }

            offset = chunkEnd;
        }

        return totalCreated;
    }

    void PhysicsWorld::AddBodies(std::span<const BodyID> ids, bool activate)
    {
        if (!IsInitialized() || ids.empty())
            return;

        constexpr size_t kBatchSize = 128;
        JPH::BodyID jphBatch[kBatchSize];

        JPH::BodyInterface& bi = m_impl->physicsSystem.GetBodyInterface();
        size_t offset = 0;
        while (offset < ids.size())
        {
            const size_t chunkEnd = std::min(ids.size(), offset + kBatchSize);
            int validCount = 0;
            for (size_t i = offset; i < chunkEnd; ++i)
            {
                if (ids[i].IsInvalid()) continue;
                const JPH::BodyID jphId = ToJolt(ids[i]);
                if (IsBodyValid(ids[i]) && !bi.IsAdded(jphId))
                    jphBatch[validCount++] = jphId;
            }

            if (validCount > 0)
            {
                const JPH::BroadPhase::AddState addState = bi.AddBodiesPrepare(jphBatch, validCount);
                bi.AddBodiesFinalize(
                    jphBatch,
                    validCount,
                    addState,
                    activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
            }
            offset = chunkEnd;
        }
    }

    void PhysicsWorld::RemoveBodies(std::span<const BodyID> ids)
    {
        if (!IsInitialized() || ids.empty())
            return;

        constexpr size_t kBatchSize = 128;
        JPH::BodyID jphBatch[kBatchSize];

        JPH::BodyInterface& bi = m_impl->physicsSystem.GetBodyInterface();
        size_t offset = 0;
        while (offset < ids.size())
        {
            const size_t chunkEnd = std::min(ids.size(), offset + kBatchSize);
            int validCount = 0;
            for (size_t i = offset; i < chunkEnd; ++i)
            {
                if (ids[i].IsInvalid()) continue;
                const JPH::BodyID jphId = ToJolt(ids[i]);
                if (IsBodyValid(ids[i]) && bi.IsAdded(jphId))
                    jphBatch[validCount++] = jphId;
            }
            if (validCount > 0)
                bi.RemoveBodies(jphBatch, validCount);

            offset = chunkEnd;
        }
    }

    void PhysicsWorld::DestroyBodies(std::span<const BodyID> ids)
    {
        if (!IsInitialized() || ids.empty())
            return;

        constexpr size_t kBatchSize = 128;
        JPH::BodyID toRemove[kBatchSize];
        JPH::BodyID toDestroy[kBatchSize];

        JPH::BodyInterface& bi = m_impl->physicsSystem.GetBodyInterface();
        size_t offset = 0;
        while (offset < ids.size())
        {
            const size_t chunkEnd = std::min(ids.size(), offset + kBatchSize);
            int removeCount  = 0;
            int destroyCount = 0;

            for (size_t i = offset; i < chunkEnd; ++i)
            {
                if (ids[i].IsInvalid() || !IsBodyValid(ids[i]))
                    continue;

                const JPH::BodyID jphId = ToJolt(ids[i]);
                if (bi.IsAdded(jphId))
                    toRemove[removeCount++] = jphId;

                m_impl->ClearBodyCache(jphId);
                toDestroy[destroyCount++] = jphId;
            }

            if (removeCount > 0)
                bi.RemoveBodies(toRemove, removeCount);
            if (destroyCount > 0)
                bi.DestroyBodies(toDestroy, destroyCount);

            offset = chunkEnd;
        }
    }

    // -------------------------------------------------------------------------
    // Body State & Manipulation
    // -------------------------------------------------------------------------

    void PhysicsWorld::SetPositionAndRotation(BodyID id,
                                              const Vec3& position,
                                              const Quat& rotation,
                                              bool activate)
    {
        if (!IsInitialized() || id.IsInvalid())
            return;

        m_impl->physicsSystem.GetBodyInterface().SetPositionAndRotation(
            ToJolt(id),
            ToJoltR(position),
            ToJolt(rotation),
            activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
    }

    void PhysicsWorld::MoveKinematic(BodyID id,
                                     const Vec3& targetPosition,
                                     const Quat& targetRotation,
                                     float fixedDt)
    {
        if (!IsInitialized() || id.IsInvalid())
            return;

        if (fixedDt <= 0.0f)
        {
            SetPositionAndRotation(id, targetPosition, targetRotation, true);
            return;
        }

        m_impl->physicsSystem.GetBodyInterface().MoveKinematic(
            ToJolt(id),
            ToJoltR(targetPosition),
            ToJolt(targetRotation),
            fixedDt);
    }

    void PhysicsWorld::SetLinearVelocity(BodyID id, const Vec3& velocity)
    {
        if (!IsInitialized() || id.IsInvalid())
            return;
        m_impl->physicsSystem.GetBodyInterface().SetLinearVelocity(ToJolt(id), ToJolt(velocity));
    }

    void PhysicsWorld::SetAngularVelocity(BodyID id, const Vec3& angularVelocity)
    {
        if (!IsInitialized() || id.IsInvalid())
            return;
        m_impl->physicsSystem.GetBodyInterface().SetAngularVelocity(ToJolt(id), ToJolt(angularVelocity));
    }

    void PhysicsWorld::AddForce(BodyID id, const Vec3& force)
    {
        if (!IsInitialized() || id.IsInvalid())
            return;
        m_impl->physicsSystem.GetBodyInterface().AddForce(ToJolt(id), ToJolt(force));
    }

    void PhysicsWorld::AddImpulse(BodyID id, const Vec3& impulse)
    {
        if (!IsInitialized() || id.IsInvalid())
            return;
        m_impl->physicsSystem.GetBodyInterface().AddImpulse(ToJolt(id), ToJolt(impulse));
    }

    void PhysicsWorld::ActivateBody(BodyID id)
    {
        if (!IsInitialized() || id.IsInvalid())
            return;
        m_impl->physicsSystem.GetBodyInterface().ActivateBody(ToJolt(id));
    }

    bool PhysicsWorld::IsBodyValid(BodyID id) const noexcept
    {
        if (!IsInitialized() || id.IsInvalid())
            return false;

        const JPH::BodyID jphId = ToJolt(id);
        const uint32_t idx = jphId.GetIndex();
        if (idx >= m_impl->bodyEntityCache.size())
            return false;

        // Fast-path check against our tracked body cache before locking
        if (m_impl->bodyEntityCache[idx].bodyIdRaw != id.value)
            return false;

        JPH::BodyLockRead lock(m_impl->physicsSystem.GetBodyLockInterface(), jphId);
        return lock.Succeeded();
    }

    bool PhysicsWorld::IsBodyActive(BodyID id) const noexcept
    {
        if (!IsInitialized() || id.IsInvalid())
            return false;
        return m_impl->physicsSystem.GetBodyInterface().IsActive(ToJolt(id));
    }

    MotionType PhysicsWorld::GetMotionType(BodyID id) const noexcept
    {
        if (!IsInitialized() || id.IsInvalid())
            return MotionType::Static;
        return ToZero(m_impl->physicsSystem.GetBodyInterface().GetMotionType(ToJolt(id)));
    }

    Vec3 PhysicsWorld::GetPosition(BodyID id) const noexcept
    {
        if (!IsInitialized() || id.IsInvalid())
            return Vec3{};
        return ToZero(m_impl->physicsSystem.GetBodyInterface().GetPosition(ToJolt(id)));
    }

    Quat PhysicsWorld::GetRotation(BodyID id) const noexcept
    {
        if (!IsInitialized() || id.IsInvalid())
            return Quat::Identity();
        return ToZero(m_impl->physicsSystem.GetBodyInterface().GetRotation(ToJolt(id)));
    }

    Vec3 PhysicsWorld::GetLinearVelocity(BodyID id) const noexcept
    {
        if (!IsInitialized() || id.IsInvalid())
            return Vec3{};
        return ToZero(m_impl->physicsSystem.GetBodyInterface().GetLinearVelocity(ToJolt(id)));
    }

    Vec3 PhysicsWorld::GetAngularVelocity(BodyID id) const noexcept
    {
        if (!IsInitialized() || id.IsInvalid())
            return Vec3{};
        return ToZero(m_impl->physicsSystem.GetBodyInterface().GetAngularVelocity(ToJolt(id)));
    }

    Entity PhysicsWorld::GetUserData(BodyID id) const noexcept
    {
        if (!IsInitialized() || id.IsInvalid())
            return Invalid;
        return static_cast<Entity>(m_impl->physicsSystem.GetBodyInterface().GetUserData(ToJolt(id)));
    }

    void PhysicsWorld::SetUserData(BodyID id, Entity entity) noexcept
    {
        if (!IsInitialized() || id.IsInvalid())
            return;

        const JPH::BodyID jphId = ToJolt(id);
        m_impl->physicsSystem.GetBodyInterface().SetUserData(jphId, static_cast<JPH::uint64>(entity));
        const uint32_t idx = jphId.GetIndex();
        if (idx < m_impl->bodyEntityCache.size() &&
            m_impl->bodyEntityCache[idx].bodyIdRaw == id.value)
        {
            m_impl->bodyEntityCache[idx].entity = entity;
        }
    }

    uint32_t PhysicsWorld::GetBodyCount() const noexcept
    {
        if (!IsInitialized())
            return 0;
        return static_cast<uint32_t>(m_impl->physicsSystem.GetNumBodies());
    }

    uint32_t PhysicsWorld::GetActiveBodyCount() const noexcept
    {
        if (!IsInitialized())
            return 0;
        return m_impl->physicsSystem.GetNumActiveBodies(JPH::EBodyType::RigidBody);
    }

    uint32_t PhysicsWorld::ReadActiveDynamicBodies(uint32_t& inOutCursor,
                                                   std::span<ActiveBodyState> outStates) const noexcept
    {
        if (!IsInitialized() || outStates.empty())
            return 0;

        const uint32_t totalActive = m_impl->physicsSystem.GetNumActiveBodies(JPH::EBodyType::RigidBody);
        if (inOutCursor >= totalActive)
            return 0;

        const JPH::BodyID* activeIds =
            m_impl->physicsSystem.GetActiveBodiesUnsafe(JPH::EBodyType::RigidBody);
        if (!activeIds)
        {
            inOutCursor = totalActive;
            return 0;
        }

        const auto& lockInterface = m_impl->physicsSystem.GetBodyLockInterfaceNoLock();
        const uint32_t capacity   = static_cast<uint32_t>(outStates.size());
        uint32_t written          = 0;

        while (inOutCursor < totalActive && written < capacity)
        {
            const JPH::BodyID jphId = activeIds[inOutCursor++];
            JPH::BodyLockRead lock(lockInterface, jphId);
            if (!lock.Succeeded())
                continue;

            const JPH::Body& body = lock.GetBody();
            if (!body.IsDynamic())
                continue;

            ActiveBodyState& dst = outStates[written++];
            dst.bodyId           = ToZero(jphId);
            dst.entity           = static_cast<Entity>(body.GetUserData());
            dst.position         = ToZero(body.GetPosition());
            dst.rotation         = ToZero(body.GetRotation());
            dst.linearVelocity   = ToZero(body.GetLinearVelocity());
            dst.angularVelocity  = ToZero(body.GetAngularVelocity());
            dst.motion           = MotionType::Dynamic;
        }

        return written;
    }

    // -------------------------------------------------------------------------
    // Zero-Allocation Spatial Queries
    // -------------------------------------------------------------------------

    bool PhysicsWorld::Raycast(const Vec3& origin,
                               const Vec3& dir,
                               float maxDist,
                               LayerMask layerMask,
                               HitResult& outHit) const noexcept
    {
        if (!IsInitialized() || maxDist <= 0.0f)
            return false;

        const float lenSq = dir.LengthSquared();
        if (lenSq < 1e-12f)
            return false;

        const Vec3 normDir = dir / std::sqrt(lenSq);
        const JPH::RRayCast ray(ToJoltR(origin), ToJolt(normDir * maxDist));

        JPH::RayCastResult hit;
        const LayerMaskBPFilter     bpFilter(layerMask);
        const LayerMaskObjectFilter objFilter(layerMask);

        const auto& nq = m_impl->physicsSystem.GetNarrowPhaseQuery();
        if (!nq.CastRay(ray, hit, bpFilter, objFilter))
            return false;

        const JPH::RVec3 hitPos = ray.GetPointOnRay(hit.mFraction);
        outHit.bodyId     = ToZero(hit.mBodyID);
        outHit.fraction   = hit.mFraction;
        outHit.distance   = hit.mFraction * maxDist;
        outHit.position   = ToZero(hitPos);
        outHit.subShapeId = hit.mSubShapeID2.GetValue();
        outHit.entity     = Invalid;
        outHit.normal     = Vec3{ 0.0f, 1.0f, 0.0f };

        JPH::BodyLockRead lock(m_impl->physicsSystem.GetBodyLockInterface(), hit.mBodyID);
        if (lock.Succeeded())
        {
            const JPH::Body& body = lock.GetBody();
            outHit.entity = static_cast<Entity>(body.GetUserData());
            outHit.normal = ToZero(body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, hitPos));
        }

        return true;
    }

    uint32_t PhysicsWorld::RaycastAll(const Vec3& origin,
                                      const Vec3& dir,
                                      float maxDist,
                                      LayerMask layerMask,
                                      std::span<HitResult> outHits) const noexcept
    {
        if (!IsInitialized() || maxDist <= 0.0f || outHits.empty())
            return 0;

        const float lenSq = dir.LengthSquared();
        if (lenSq < 1e-12f)
            return 0;

        const Vec3 normDir = dir / std::sqrt(lenSq);
        const JPH::RRayCast ray(ToJoltR(origin), ToJolt(normDir * maxDist));

        JPH::RayCastSettings raySettings;
        ZeroCastRayAllCollector collector(outHits);
        const LayerMaskBPFilter     bpFilter(layerMask);
        const LayerMaskObjectFilter objFilter(layerMask);

        m_impl->physicsSystem.GetNarrowPhaseQuery().CastRay(
            ray, raySettings, collector, bpFilter, objFilter);

        const uint32_t hitCount = collector.Count();
        if (hitCount == 0)
            return 0;

        std::sort(outHits.begin(), outHits.begin() + hitCount,
            [](const HitResult& a, const HitResult& b) {
                return a.fraction < b.fraction;
            });

        const auto& lockInterface = m_impl->physicsSystem.GetBodyLockInterface();
        for (uint32_t i = 0; i < hitCount; ++i)
        {
            HitResult& h = outHits[i];
            const JPH::RVec3 hitPos = ray.GetPointOnRay(h.fraction);
            h.position = ToZero(hitPos);
            h.distance = h.fraction * maxDist;
            h.entity   = Invalid;
            h.normal   = Vec3{ 0.0f, 1.0f, 0.0f };

            const JPH::BodyID jphId = ToJolt(h.bodyId);
            JPH::BodyLockRead lock(lockInterface, jphId);
            if (lock.Succeeded())
            {
                const JPH::Body& body = lock.GetBody();
                h.entity = static_cast<Entity>(body.GetUserData());
                JPH::SubShapeID subId;
                subId.SetValue(h.subShapeId);
                h.normal = ToZero(body.GetWorldSpaceSurfaceNormal(subId, hitPos));
            }
        }

        return hitCount;
    }

    uint32_t PhysicsWorld::OverlapSphere(const Vec3& center,
                                         float radius,
                                         LayerMask layerMask,
                                         std::span<BodyID> outBodies) const noexcept
    {
        if (!IsInitialized() || radius <= 0.0f || outBodies.empty())
            return 0;

        JPH::SphereShape sphere(std::max(radius, 1e-4f));
        sphere.SetEmbedded();

        const JPH::RVec3 baseOffset = ToJoltR(center);
        const JPH::RMat44 transform = JPH::RMat44::sTranslation(baseOffset);

        JPH::CollideShapeSettings settings;
        ZeroOverlapCollector collector(outBodies);
        const LayerMaskBPFilter     bpFilter(layerMask);
        const LayerMaskObjectFilter objFilter(layerMask);

        m_impl->physicsSystem.GetNarrowPhaseQuery().CollideShape(
            &sphere,
            JPH::Vec3::sReplicate(1.0f),
            transform,
            settings,
            baseOffset,
            collector,
            bpFilter,
            objFilter);

        return collector.Count();
    }

    uint32_t PhysicsWorld::OverlapBox(const Vec3& center,
                                      const Vec3& halfExtents,
                                      const Quat& rotation,
                                      LayerMask layerMask,
                                      std::span<BodyID> outBodies) const noexcept
    {
        if (!IsInitialized() || outBodies.empty())
            return 0;

        const float hx = std::max(std::fabs(halfExtents.x), 1e-3f);
        const float hy = std::max(std::fabs(halfExtents.y), 1e-3f);
        const float hz = std::max(std::fabs(halfExtents.z), 1e-3f);
        const float minExt = std::min({ hx, hy, hz });
        const float convex = std::min(0.05f, minExt * 0.5f);

        JPH::BoxShape box(JPH::Vec3(hx, hy, hz), convex);
        box.SetEmbedded();

        const JPH::RVec3 baseOffset = ToJoltR(center);
        const JPH::RMat44 transform = JPH::RMat44::sRotationTranslation(ToJolt(rotation), baseOffset);

        JPH::CollideShapeSettings settings;
        ZeroOverlapCollector collector(outBodies);
        const LayerMaskBPFilter     bpFilter(layerMask);
        const LayerMaskObjectFilter objFilter(layerMask);

        m_impl->physicsSystem.GetNarrowPhaseQuery().CollideShape(
            &box,
            JPH::Vec3::sReplicate(1.0f),
            transform,
            settings,
            baseOffset,
            collector,
            bpFilter,
            objFilter);

        return collector.Count();
    }

    // -------------------------------------------------------------------------
    // Zero-Allocation Contact Events
    // -------------------------------------------------------------------------

    std::span<const ContactEvent> PhysicsWorld::GetContactEvents() const noexcept
    {
        if (!IsInitialized())
            return {};
        return m_impl->contactListener.Events();
    }

    uint32_t PhysicsWorld::GetDroppedContactEventCount() const noexcept
    {
        if (!IsInitialized())
            return 0;
        return m_impl->contactListener.DroppedCount();
    }

    void PhysicsWorld::ClearContactEvents() noexcept
    {
        if (!IsInitialized())
            return;
        m_impl->contactListener.Clear();
    }
}
