#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <atomic>
#include <unordered_map>
#include <Engine/Math/Mat4.h>
#include <Engine/Math/Vec3.h>
#include <Engine/Scene/SceneTypes.h>
#include <Engine/Scene/Scene.h>
#include <concurrentqueue.h>
#include "Graphics/core/RHITypes.h"
#include "Scene/SceneCommand.h"

namespace Zero
{
    // SlotAllocator
    //   Fixed-size free-list for O(1) alloc/free of uint32_t slot indices.
    //   Index 0 is reserved as "invalid".
    //   Not thread-safe — only accessed from MainThread after FlushCommands().
    class SlotAllocator
    {
    public:
        explicit SlotAllocator(uint32_t capacity)
        {
            m_free.reserve(capacity);
            // Fill free list in reverse so first alloc returns index 1
            for (uint32_t i = capacity; i >= 1; --i)
                m_free.push_back(i);
            m_capacity = capacity;
        }

        [[nodiscard]] uint32_t Alloc()
        {
            if (m_free.empty()) return 0; // 0 == invalid
            uint32_t idx = m_free.back();
            m_free.pop_back();
            return idx;
        }

        void Free(uint32_t idx)
        {
            if (idx != 0) m_free.push_back(idx);
        }

        [[nodiscard]] uint32_t Capacity() const noexcept { return m_capacity; }
        [[nodiscard]] uint32_t Used()     const noexcept { return m_capacity - static_cast<uint32_t>(m_free.size()); }

    private:
        std::vector<uint32_t> m_free;
        uint32_t              m_capacity = 0;
    };

    // RenderableStore
    //   SoA storage for renderables. Parallel arrays — index i = same object.
    //
    //   HOT  (culling)  : bounds components, contiguous floats
    //   WARM (sorting)  : sortKeys, transformIndex
    //   COLD (draw)     : GPU handles, draw params, transforms
    //
    //   slotToIndex[] maps RenderableHandle.id → dense array index.
    //   Removal swaps with last element (swap-and-pop) to keep arrays dense.
    struct RenderableStore
    {
        static constexpr uint32_t DefaultCapacity = 4096;

        // Slot allocator — maps handle id → dense index
        SlotAllocator slots{ DefaultCapacity };

        // slotToIndex[slotId] = dense index (-1 = not present)
        std::vector<uint32_t> slotToIndex;
        // indexToSlot[denseIdx] = slot id (for swap-and-pop fixup)
        std::vector<uint32_t> indexToSlot;

        // HOT: culling
        std::vector<float> boundsMinX;
        std::vector<float> boundsMinY;
        std::vector<float> boundsMinZ;
        std::vector<float> boundsMaxX;
        std::vector<float> boundsMaxY;
        std::vector<float> boundsMaxZ;

        // WARM: sorting 
        std::vector<uint32_t> transformIndex; // index into transforms[]
        std::vector<uint64_t> sortKeys;       // pre-built, updated on change

        // COLD: draw 
        std::vector<RHI::BufferHandle> vertexBuffers;
        std::vector<RHI::BufferHandle> indexBuffers;
        std::vector<RHI::PipelineHandle> pipelines;
        std::vector<uint32_t> indexCount;
        std::vector<uint32_t> firstIndex;
        std::vector<uint32_t> vertexOffset;
        std::vector<uint16_t> materialId;
        std::vector<uint16_t> pipelineId;
        std::vector<uint8_t> castsShadow;
        std::vector<uint8_t> isTransparent;

        // Transforms — 1:1 with dense entries (each renderable owns its transform)
        std::vector<Mat4> transforms;

        // Count 
        [[nodiscard]] uint32_t Count() const noexcept
        {
            return static_cast<uint32_t>(boundsMinX.size());
        }

        void Reserve(uint32_t n)
        {
            slotToIndex.resize(slots.Capacity() + 1, UINT32_MAX);
            indexToSlot.reserve(n);
            boundsMinX.reserve(n); boundsMinY.reserve(n); boundsMinZ.reserve(n);
            boundsMaxX.reserve(n); boundsMaxY.reserve(n); boundsMaxZ.reserve(n);
            transformIndex.reserve(n); sortKeys.reserve(n);
            vertexBuffers.reserve(n); indexBuffers.reserve(n); pipelines.reserve(n);
            indexCount.reserve(n); firstIndex.reserve(n); vertexOffset.reserve(n);
            materialId.reserve(n); pipelineId.reserve(n);
            castsShadow.reserve(n); isTransparent.reserve(n);
            transforms.reserve(n);
        }

        // Insert — returns assigned slot id (= RenderableHandle.id)
        uint32_t Insert(uint32_t slotId, const RenderableDescPOD& d)
        {
            uint32_t idx = Count();
            slotToIndex[slotId] = idx;
            indexToSlot.push_back(slotId);

            boundsMinX.push_back(d.boundsMin.x);
            boundsMinY.push_back(d.boundsMin.y);
            boundsMinZ.push_back(d.boundsMin.z);
            boundsMaxX.push_back(d.boundsMax.x);
            boundsMaxY.push_back(d.boundsMax.y);
            boundsMaxZ.push_back(d.boundsMax.z);

            transformIndex.push_back(idx);
            sortKeys.push_back(0u);
            transforms.push_back(d.transform);

            // Reconstruct RHI handles from stored IDs
            RHI::BufferHandle   vb;  vb.idx = d.vertexBufferId;
            RHI::BufferHandle   ib;  ib.idx = d.indexBufferId;
            RHI::PipelineHandle pso; pso.idx = d.pipelineId_;

            vertexBuffers.push_back(vb);
            indexBuffers.push_back(ib);
            pipelines.push_back(pso);
            indexCount.push_back(d.indexCount);
            firstIndex.push_back(d.firstIndex);
            vertexOffset.push_back(d.vertexOffset);
            materialId.push_back(d.materialId);
            pipelineId.push_back(d.pipelineId);
            castsShadow.push_back(d.castsShadow);
            isTransparent.push_back(d.isTransparent);

            return idx;
        }

        // Swap-and-pop removal — O(1), keeps arrays dense
        void Remove(uint32_t slot)
        {
            uint32_t idx = slotToIndex[slot];
            uint32_t last = Count() - 1;

            if (idx != last)
            {
                // Move last element into the removed slot
                uint32_t lastSlot = indexToSlot[last];

                boundsMinX[idx] = boundsMinX[last]; boundsMinY[idx] = boundsMinY[last]; boundsMinZ[idx] = boundsMinZ[last];
                boundsMaxX[idx] = boundsMaxX[last]; boundsMaxY[idx] = boundsMaxY[last]; boundsMaxZ[idx] = boundsMaxZ[last];
                transformIndex[idx] = idx; // rebased — transform is 1:1 with dense index now
                sortKeys[idx] = sortKeys[last];
                vertexBuffers[idx] = vertexBuffers[last];
                indexBuffers[idx] = indexBuffers[last];
                pipelines[idx] = pipelines[last];
                indexCount[idx] = indexCount[last];
                firstIndex[idx] = firstIndex[last];
                vertexOffset[idx] = vertexOffset[last];
                materialId[idx] = materialId[last];
                pipelineId[idx] = pipelineId[last];
                castsShadow[idx] = castsShadow[last];
                isTransparent[idx] = isTransparent[last];
                transforms[idx] = transforms[last];

                // Fixup index mapping for moved element
                slotToIndex[lastSlot] = idx;
                indexToSlot[idx] = lastSlot;
            }

            // Pop last
            boundsMinX.pop_back(); boundsMinY.pop_back(); boundsMinZ.pop_back();
            boundsMaxX.pop_back(); boundsMaxY.pop_back(); boundsMaxZ.pop_back();
            transformIndex.pop_back(); sortKeys.pop_back();
            vertexBuffers.pop_back(); indexBuffers.pop_back(); pipelines.pop_back();
            indexCount.pop_back(); firstIndex.pop_back(); vertexOffset.pop_back();
            materialId.pop_back(); pipelineId.pop_back();
            castsShadow.pop_back(); isTransparent.pop_back();
            transforms.pop_back();
            indexToSlot.pop_back();

            slotToIndex[slot] = UINT32_MAX;
            slots.Free(slot);
        }

        void UpdateTransform(uint32_t slot, const Mat4& t)
        {
            uint32_t idx = slotToIndex[slot];
            transforms[idx] = t;
        }

        void UpdateBounds(uint32_t slot, const Vec3& mn, const Vec3& mx)
        {
            uint32_t idx = slotToIndex[slot];
            boundsMinX[idx] = mn.x; boundsMinY[idx] = mn.y; boundsMinZ[idx] = mn.z;
            boundsMaxX[idx] = mx.x; boundsMaxY[idx] = mx.y; boundsMaxZ[idx] = mx.z;
        }
    };

    // LightStore
    //   Slot-based dense storage for lights.
    struct LightStore
    {
        static constexpr uint32_t DirBit = 1u << 31;
        static constexpr uint32_t TypeMask = ~DirBit;

        // Directional lights
        SlotAllocator dirSlots{ 64 };
        std::vector<uint32_t> dirSlotToIndex;
        std::vector<uint32_t> dirIndexToSlot;
        std::vector<DirectionalLightDesc> dirLights;

        // Point lights
        SlotAllocator pointSlots{ 256 };
        std::vector<uint32_t> pointSlotToIndex;
        std::vector<uint32_t> pointIndexToSlot;
        std::vector<PointLightDesc> pointLights;

        LightStore()
        {
            dirSlotToIndex.resize(dirSlots.Capacity() + 1, UINT32_MAX);
            pointSlotToIndex.resize(pointSlots.Capacity() + 1, UINT32_MAX);
        }

        uint32_t AddDirectionalLight(uint32_t handleId, const DirectionalLightDesc& desc)
        {
            uint32_t slot = handleId;
            if (slot >= dirSlotToIndex.size())
                dirSlotToIndex.resize(slot + 1, UINT32_MAX);

            uint32_t idx = static_cast<uint32_t>(dirLights.size());
            dirSlotToIndex[slot] = idx;
            dirIndexToSlot.push_back(slot);
            dirLights.push_back(desc);
            return idx;
        }

        uint32_t AddPointLight(uint32_t handleId, const PointLightDesc& desc)
        {
            uint32_t slot = handleId;
            if (slot >= pointSlotToIndex.size())
                pointSlotToIndex.resize(slot + 1, UINT32_MAX);

            uint32_t idx = static_cast<uint32_t>(pointLights.size());
            pointSlotToIndex[slot] = idx;
            pointIndexToSlot.push_back(slot);
            pointLights.push_back(desc);
            return idx;
        }

        void RemoveDirectionalLight(uint32_t handleId)
        {
            if (handleId >= dirSlotToIndex.size()) return;
            uint32_t idx = dirSlotToIndex[handleId];
            if (idx == UINT32_MAX) return;

            uint32_t last = static_cast<uint32_t>(dirLights.size()) - 1;
            if (idx != last)
            {
                uint32_t lastSlot = dirIndexToSlot[last];
                dirLights[idx] = dirLights[last];
                dirSlotToIndex[lastSlot] = idx;
                dirIndexToSlot[idx] = lastSlot;
            }
            dirLights.pop_back();
            dirIndexToSlot.pop_back();
            dirSlotToIndex[handleId] = UINT32_MAX;
        }

        void RemovePointLight(uint32_t handleId)
        {
            if (handleId >= pointSlotToIndex.size()) return;
            uint32_t idx = pointSlotToIndex[handleId];
            if (idx == UINT32_MAX) return;

            uint32_t last = static_cast<uint32_t>(pointLights.size()) - 1;
            if (idx != last)
            {
                uint32_t lastSlot = pointIndexToSlot[last];
                pointLights[idx] = pointLights[last];
                pointSlotToIndex[lastSlot] = idx;
                pointIndexToSlot[idx] = lastSlot;
            }
            pointLights.pop_back();
            pointIndexToSlot.pop_back();
            pointSlotToIndex[handleId] = UINT32_MAX;
        }

        void UpdatePointLightPosition(uint32_t handleId, const Vec3& pos)
        {
            if (handleId >= pointSlotToIndex.size()) return;
            uint32_t idx = pointSlotToIndex[handleId];
            if (idx == UINT32_MAX) return;
            pointLights[idx].position = pos;
        }
    };

    struct Scene::Impl
    {
        uint32_t    id;
        std::string name;
        RenderPath  renderPath = RenderPath::CPUDriven;  // set at creation

        RenderableStore renderables;
        LightStore      lights;
        CameraDesc      camera{};

        moodycamel::ConcurrentQueue<SceneCommand> commands;
        std::atomic<uint32_t> nextHandleId{ 1 };

        // Track light type per handle (true = directional, false = point)
        std::unordered_map<uint32_t, bool> lightTypeMap;

        Impl(uint32_t id_, std::string_view name_, RenderPath path)
            : id(id_), name(name_), renderPath(path)
        {
            renderables.Reserve(RenderableStore::DefaultCapacity);
        }
    };
}
