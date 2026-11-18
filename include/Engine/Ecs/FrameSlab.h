#pragma once
#include <cstdint>
#include <cstring>
#include <atomic>
#include <Engine/Core.h>
#include <Engine/Platform.h>
#include <Engine/Log.h>

namespace Zero
{
    // FrameSlab — CAS-based aligned bump allocator.
    //
    // Thread-safe: multiple job threads may Allocate() concurrently.
    // Reset() must be called ONLY after FlushCommands() fully completes
    // payload data is read during flush.
    //
    // alignment must be a power of two and <= the base alignment passed to
    // the constructor (default: 64, matching a cache line).
    class FrameSlab
    {
    public:
        explicit FrameSlab(uint32_t capacityBytes, uint32_t baseAlignment = 64)
            : m_capacity(capacityBytes)
            , m_buffer(static_cast<uint8_t*>(ZR_ALIGNED_MALLOC(capacityBytes, baseAlignment)))
        {
            ZERO_CORE_ASSERT(m_buffer != nullptr, "FrameSlab allocation failed");
            ZERO_CORE_ASSERT(baseAlignment != 0, (baseAlignment & (baseAlignment - 1)) == 0
                   && "baseAlignment must be a power of two");
        }

        ~FrameSlab() { ZR_ALIGNED_FREE(m_buffer); }

        FrameSlab(const FrameSlab&) = delete;
        FrameSlab& operator=(const FrameSlab&) = delete;

        // Thread-safe aligned allocation.
        // Returns the byte offset of the written payload in the slab buffer,
        // or UINT32_MAX if the allocation would exceed capacity. Callers must
        // handle overflow (e.g. heap fallback) instead of corrupting memory.
        [[nodiscard]] uint32_t Allocate(const void* src, uint32_t size, uint32_t alignment) noexcept
        {
            ZERO_ASSERT(alignment != 0 && (alignment & (alignment - 1)) == 0, "alignment must be a power of two");
            ZERO_ASSERT(size > 0, "Size must be greater than 0");

            uint32_t cur = m_cursor.load(std::memory_order_relaxed);
            uint32_t offset;
            uint32_t next;

            for (;;)
            {
                offset = (cur + alignment - 1u) & ~(alignment - 1u);
                next   = offset + size;
                if (next > m_capacity)
                    return UINT32_MAX;   // overflow — caller must fall back
                if (m_cursor.compare_exchange_weak(cur, next,
                        std::memory_order_release,
                        std::memory_order_relaxed))
                    break;
            }

            std::memcpy(m_buffer + offset, src, size);
            return offset;
        }

        // Typed convenience — deduces size and alignment from T.
        template<typename T>
        [[nodiscard]] uint32_t Store(const T& value) noexcept
        {
            return Allocate(&value, static_cast<uint32_t>(sizeof(T)),
                            static_cast<uint32_t>(alignof(T)));
        }

        // Typed read-back — caller must pass the offset returned by Store<T>.
        template<typename T>
        [[nodiscard]] const T& As(uint32_t offset) const noexcept
        {
            assert(offset + sizeof(T) <= m_capacity);
            return *reinterpret_cast<const T*>(m_buffer + offset);
        }

        // Main-thread only. Must be called AFTER FlushCommands() completes.
        void Reset() noexcept { m_cursor.store(0, std::memory_order_relaxed); }

        [[nodiscard]] uint32_t Used()     const noexcept { return m_cursor.load(std::memory_order_relaxed); }
        [[nodiscard]] uint32_t Capacity() const noexcept { return m_capacity; }

    private:
        const uint32_t        m_capacity;
        uint8_t*              m_buffer;
        std::atomic<uint32_t> m_cursor{ 0 };
    };
}
