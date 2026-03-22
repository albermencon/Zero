#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <atomic>
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
        std::vector<RHI::BufferHandle>   vertexBuffers;
        std::vector<RHI::BufferHandle>   indexBuffers;
        std::vector<RHI::PipelineHandle> pipelines;
        std::vector<uint32_t>            indexCount;
        std::vector<uint32_t>            firstIndex;
        std::vector<uint32_t>            vertexOffset;
        std::vector<uint16_t>            materialId;
        std::vector<uint16_t>            pipelineId;
        std::vector<bool>                castsShadow;
        std::vector<bool>                isTransparent;

        // Transforms indexed by transformIndex — kept cold
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

            const uint32_t tIdx = static_cast<uint32_t>(transforms.size());
            transformIndex.push_back(tIdx);
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
                transformIndex[idx] = transformIndex[last];
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

                // Fixup index mapping for moved element
                slotToIndex[lastSlot] = idx;
                indexToSlot[idx] = lastSlot;

                // Move transform (swap, not copy — avoids redundant Mat4 in transforms[])
                transforms[transformIndex[idx]] = transforms[transformIndex[last]];
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
            transforms[transformIndex[idx]] = t;
        }

        void UpdateBounds(uint32_t slot, const Vec3& mn, const Vec3& mx)
        {
            uint32_t idx = slotToIndex[slot];
            boundsMinX[idx] = mn.x; boundsMinY[idx] = mn.y; boundsMinZ[idx] = mn.z;
            boundsMaxX[idx] = mx.x; boundsMaxY[idx] = mx.y; boundsMaxZ[idx] = mx.z;
        }
    };

    // LightStore
    //   AoS for lights — count is small (rarely > 64), SIMD not needed.
    struct LightStore
    {
        SlotAllocator slots{ 256 };
        std::vector<uint32_t>            slotToIndex;
        std::vector<uint32_t>            indexToSlot;
        std::vector<DirectionalLightDesc> dirLights;
        std::vector<PointLightDesc>       pointLights;

        // Separate slot spaces for dir vs point lights using high bit
        static constexpr uint32_t DirBit = 1u << 31;
        static constexpr uint32_t TypeMask = ~DirBit;
    };

    struct Scene::Impl
    {
        uint32_t    id;
        std::string name;

        RenderableStore renderables;
        LightStore      lights;
        CameraDesc      camera{};

        moodycamel::ConcurrentQueue<SceneCommand> commands;
        std::atomic<uint32_t> nextHandleId{ 1 };

        Impl(uint32_t id_, std::string_view name_)
            : id(id_), name(name_)
        {
            renderables.Reserve(RenderableStore::DefaultCapacity);
        }
    };
}
