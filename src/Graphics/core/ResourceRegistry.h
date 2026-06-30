#pragma once

#include <vector>
#include <Engine/Thread/Mutex.h>
#include <Engine/Thread/ScopedLock.h>
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
            Zero::ScopedLock lock(m_mutex);

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
            uint32_t index = handle.GetIndex();
            if (index == 0 || index >= Capacity) return;

            Detail::ResourceSlot<BackendType>& slot = m_slots[index];
            
            // Only deallocate if generation matches
            if (slot.generation.load(std::memory_order_acquire) == handle.GetGeneration())
            {
                slot.state.store(ResourceState::DestroyPending, std::memory_order_release);
            }
        }

        // Called by RenderThread after safe destruction.
        void ReclaimSlot(HandleType handle)
        {
            uint32_t index = handle.GetIndex();
            if (index == 0 || index >= Capacity) return;

            Zero::ScopedLock lock(m_mutex);
            Detail::ResourceSlot<BackendType>& slot = m_slots[index];

            if (slot.generation.load(std::memory_order_relaxed) != handle.GetGeneration())
                return;

            if (slot.state.load(std::memory_order_relaxed) != ResourceState::DestroyPending)
                return;

            slot.state.store(ResourceState::Invalid, std::memory_order_release);
            slot.generation.store(slot.generation.load(std::memory_order_relaxed) + 1, std::memory_order_release); // Increment generation to invalidate old handles

#ifdef ZERO_DEBUG
            slot.debugName.clear();
#endif

            m_freeIndices.push_back(index);
        }

        // Called if resource creation fails, skipping DestroyPending
        void MarkFailed(HandleType handle)
        {
            uint32_t index = handle.GetIndex();
            if (index == 0 || index >= Capacity) return;

            Zero::ScopedLock lock(m_mutex);
            Detail::ResourceSlot<BackendType>& slot = m_slots[index];

            if (slot.generation.load(std::memory_order_relaxed) != handle.GetGeneration())
                return;

            // Ensure we only mark failed if it was pending
            if (slot.state.load(std::memory_order_relaxed) != ResourceState::Pending)
                return;

            slot.state.store(ResourceState::Invalid, std::memory_order_release);
            slot.generation.store(slot.generation.load(std::memory_order_relaxed) + 1, std::memory_order_release);

#ifdef ZERO_DEBUG
            slot.debugName.clear();
#endif
            m_freeIndices.push_back(index);
        }

        // Called by RenderThread when resource is created.
        void MarkReady(HandleType handle, const BackendType& backendResource)
        {
            uint32_t index = handle.GetIndex();
            if (index == 0 || index >= Capacity) return;
            
            Detail::ResourceSlot<BackendType>& slot = m_slots[index];
            if (slot.generation.load(std::memory_order_acquire) != handle.GetGeneration())
            {
                return; // stale handle request abort
            }

            slot.resource = backendResource;
            slot.state.store(ResourceState::Ready, std::memory_order_release);
        }
        
#ifdef ZERO_DEBUG
        void SetDebugName(uint32_t index, const std::string& name)
        {
            if (index == 0 || index >= Capacity) return;
            Zero::ScopedLock lock(m_mutex);
            m_slots[index].debugName = name;
        }
#endif

        // Client query. Thread-safe lock-free read.
        ResourceState GetState(HandleType handle) const
        {
            uint32_t index = handle.GetIndex();
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
            uint32_t index = handle.GetIndex();
            if (index == 0 || index >= Capacity) return BackendType{};

            const Detail::ResourceSlot<BackendType>& slot = m_slots[index];
            if (slot.generation.load(std::memory_order_acquire) != handle.GetGeneration())
            {
                return BackendType{};
            }

            ResourceState currentState = slot.state.load(std::memory_order_acquire);
            if (currentState == ResourceState::Ready || currentState == ResourceState::DestroyPending)
            {
                return slot.resource;
            }

            return BackendType{};
        }

        template<typename Func>
        void ForEach(Func func)
        {
            Zero::ScopedLock lock(m_mutex);
            for (uint32_t i = 1; i < Capacity; ++i)
            {
                ResourceState currentState = m_slots[i].state.load(std::memory_order_relaxed);
                if (currentState == ResourceState::Ready || currentState == ResourceState::DestroyPending)
                {
                    HandleType h;
                    h.value = (static_cast<uint32_t>(m_slots[i].generation.load(std::memory_order_relaxed)) << 16) | i;
                    func(h, m_slots[i].resource);
                }
            }
        }

    private:
        std::unique_ptr<Detail::ResourceSlot<BackendType>[]> m_slots;
        
        Zero::Mutex m_mutex;
        std::vector<uint16_t> m_freeIndices;
    };
}
