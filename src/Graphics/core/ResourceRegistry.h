#pragma once

#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <string>
#include <memory>
#include <Engine/Graphics/ResourceHandle.h>

namespace Zero
{
    namespace Detail
    {
        template<typename BackendType>
        struct ResourceSlot
        {
            BackendType resource{}; // Backend handles or pointers
            std::atomic<ResourceState> state{ ResourceState::Invalid };
            std::atomic<uint16_t> generation{1};

#ifdef ZERO_DEBUG
            std::string debugName;
#endif
        };
    }

    template<typename BackendType, typename HandleType, uint32_t Capacity>
    class ResourceRegistry
    {
    public:
        static_assert(Capacity <= 65536, "Capacity exceeds 16-bit index range.");

        ResourceRegistry()
        {
            m_slots = std::make_unique<Detail::ResourceSlot<BackendType>[]>(Capacity);
            m_freeIndices.reserve(Capacity);
            // Populate free indices (Capacity-1 down to 1). Index 0 is reserved.
            for (uint16_t i = Capacity - 1; i > 0; --i)
            {
                m_freeIndices.push_back(i);
            }
        }

        ~ResourceRegistry() = default;

        // Called by any worker thread. Thread-safe.
        HandleType Allocate()
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            if (m_freeIndices.empty())
            {
                // Out of memory for this resource type.
                return HandleType{ 0 };
            }

            uint16_t index = m_freeIndices.back();
            m_freeIndices.pop_back();

            Detail::ResourceSlot<BackendType>& slot = m_slots[index];
            slot.state.store(ResourceState::Pending, std::memory_order_release);

            HandleType handle;
            handle.value = (static_cast<uint32_t>(slot.generation.load(std::memory_order_relaxed)) << 16) | index;
            return handle;
        }

        // Sets state to DestroyPending. Resource is NOT freed here.
        // Thread-safe.
        void Deallocate(HandleType handle)
        {
            uint16_t index = handle.GetIndex();
            if (index == 0 || index >= Capacity) return;

            Detail::ResourceSlot<BackendType>& slot = m_slots[index];
            
            // Only deallocate if generation matches
            if (slot.generation.load(std::memory_order_acquire) == handle.GetGeneration())
            {
                slot.state.store(ResourceState::DestroyPending, std::memory_order_release);
            }
        }

        // Called by RenderThread after safe destruction.
        void ReclaimSlot(uint16_t index)
        {
            if (index == 0 || index >= Capacity) return;

            std::lock_guard<std::mutex> lock(m_mutex);
            Detail::ResourceSlot<BackendType>& slot = m_slots[index];

            if (slot.state.load(std::memory_order_relaxed) != ResourceState::DestroyPending)
                return;

            slot.state.store(ResourceState::Invalid, std::memory_order_release);
            slot.generation.store(slot.generation.load(std::memory_order_relaxed) + 1, std::memory_order_release); // Increment generation to invalidate old handles

#ifdef ZERO_DEBUG
            slot.debugName.clear();
#endif

            m_freeIndices.push_back(index);
        }

        // Called by RenderThread when resource is created.
        void MarkReady(uint16_t index, const BackendType& backendResource)
        {
            if (index == 0 || index >= Capacity) return;
            
            Detail::ResourceSlot<BackendType>& slot = m_slots[index];
            slot.resource = backendResource;
            slot.state.store(ResourceState::Ready, std::memory_order_release);
        }
        
#ifdef ZERO_DEBUG
        void SetDebugName(uint16_t index, const std::string& name)
        {
            if (index == 0 || index >= Capacity) return;
            std::lock_guard<std::mutex> lock(m_mutex);
            m_slots[index].debugName = name;
        }
#endif

        // Client query. Thread-safe lock-free read.
        ResourceState GetState(HandleType handle) const
        {
            uint16_t index = handle.GetIndex();
            if (index == 0 || index >= Capacity) return ResourceState::Invalid;

            const Detail::ResourceSlot<BackendType>& slot = m_slots[index];
            if (slot.generation.load(std::memory_order_acquire) != handle.GetGeneration())
            {
                return ResourceState::Invalid;
            }

            return slot.state.load(std::memory_order_acquire);
        }

        // Fast internal access for RenderThread / backend. Lock-free.
        BackendType GetBackendResource(HandleType handle) const
        {
            uint16_t index = handle.GetIndex();
            if (index == 0 || index >= Capacity) return BackendType{};

            const Detail::ResourceSlot<BackendType>& slot = m_slots[index];
            if (slot.generation.load(std::memory_order_acquire) != handle.GetGeneration())
            {
                return BackendType{};
            }

            if (slot.state.load(std::memory_order_acquire) == ResourceState::Ready)
            {
                return slot.resource;
            }

            return BackendType{};
        }

    private:
        std::unique_ptr<Detail::ResourceSlot<BackendType>[]> m_slots;
        
        std::mutex m_mutex;
        std::vector<uint16_t> m_freeIndices;
    };
}
