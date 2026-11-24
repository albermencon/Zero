#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <Engine/Core.h>
#include <Engine/Platform.h>
#include <Engine/Log.h>

namespace Zero
{
    // Thread-safe: multiple job threads may Allocate() concurrently.
    // Reset() must be called ONLY after FlushCommands() fully completes
    // payload data is read during flush.
    //
    // alignment must be a power of two and <= the base alignment passed to
    // the constructor (default: 64)
    class FrameSlab
    {
    public:
        explicit FrameSlab(uint32_t capacityBytes, uint32_t baseAlignment = 64)
            : m_baseAlignment(baseAlignment)
            , m_capacity(AlignCapacity(capacityBytes, baseAlignment))
            , m_buffer(nullptr)
        {
            ZERO_CORE_ASSERT(baseAlignment != 0 && (baseAlignment & (baseAlignment - 1u)) == 0,
                "baseAlignment must be a non-zero power of two");
            ZERO_CORE_ASSERT(m_capacity > 0, "FrameSlab capacity must be greater than 0");
            if (baseAlignment != 0 && (baseAlignment & (baseAlignment - 1u)) == 0 && m_capacity > 0)
                m_buffer = static_cast<uint8_t*>(ZR_ALIGNED_MALLOC(m_capacity, m_baseAlignment));
            ZERO_CORE_ASSERT(m_buffer != nullptr, "FrameSlab allocation failed");
        }

        ~FrameSlab()
        {
            if (m_buffer)
                ZR_ALIGNED_FREE(m_buffer);
        }

        FrameSlab(const FrameSlab&) = delete;
        FrameSlab& operator=(const FrameSlab&) = delete;

        // Thread-safe aligned allocation.
        // Returns the byte offset of the written payload in the slab buffer,
        // or UINT32_MAX if the allocation would exceed capacity. Callers must
        // handle overflow (e.g. heap fallback) instead of corrupting memory.
        [[nodiscard]] uint32_t Allocate(const void* src, uint32_t size, uint32_t alignment) noexcept
        {
            ZERO_ASSERT(alignment != 0 && (alignment & (alignment - 1u)) == 0,
                "alignment must be a power of two");
            ZERO_ASSERT(alignment <= m_baseAlignment,
                "alignment exceeds FrameSlab baseAlignment");
            ZERO_ASSERT(size > 0, "Size must be greater than 0");

            if (!m_buffer || !src || size == 0 ||
                alignment == 0 || (alignment & (alignment - 1u)) != 0 ||
                alignment > m_baseAlignment)
            {
                return UINT32_MAX;
            }

            uint32_t cur = m_cursor.load(std::memory_order_relaxed);
            const size_t alignMask = static_cast<size_t>(alignment) - 1u;
            const size_t cap       = static_cast<size_t>(m_capacity);
            size_t offset = 0;
            size_t next   = 0;

            for (;;)
            {
                offset = (static_cast<size_t>(cur) + alignMask) & ~alignMask;
                next   = offset + static_cast<size_t>(size);
                if (next > cap)
                    return UINT32_MAX;   // overflow — caller must fall back
                if (m_cursor.compare_exchange_weak(cur, static_cast<uint32_t>(next),
                        std::memory_order_release,
                        std::memory_order_relaxed))
                    break;
            }

            std::memcpy(m_buffer + offset, src, size);
            return static_cast<uint32_t>(offset);
        }

        // Typed convenience — deduces size and alignment from T.
        template<typename T>
        [[nodiscard]] uint32_t Store(const T& value) noexcept
        {
            return Allocate(&value, static_cast<uint32_t>(sizeof(T)),
                            static_cast<uint32_t>(alignof(T)));
        }

        // Raw byte access for copying payloads out via std::memcpy (no typed pointer aliasing).
        [[nodiscard]] const uint8_t* Data(uint32_t offset, uint32_t size = 1) const noexcept
        {
            ZERO_ASSERT(static_cast<size_t>(offset) + static_cast<size_t>(size) <= static_cast<size_t>(m_capacity),
                "FrameSlab::Data out of bounds");
            if (!m_buffer || static_cast<size_t>(offset) + static_cast<size_t>(size) > static_cast<size_t>(m_capacity))
                return nullptr;
            return m_buffer + offset;
        }

        // Main-thread only. Must be called AFTER FlushCommands() completes.
        void Reset() noexcept { m_cursor.store(0, std::memory_order_relaxed); }

        [[nodiscard]] uint32_t Used()     const noexcept { return m_cursor.load(std::memory_order_relaxed); }
        [[nodiscard]] uint32_t Capacity() const noexcept { return m_capacity; }

    private:
        static uint32_t AlignCapacity(uint32_t capacityBytes, uint32_t baseAlignment) noexcept
        {
            if (baseAlignment == 0 || (baseAlignment & (baseAlignment - 1u)) != 0)
                return capacityBytes;
            const size_t mask    = static_cast<size_t>(baseAlignment) - 1u;
            const size_t aligned = (static_cast<size_t>(capacityBytes) + mask) & ~mask;
            if (aligned > UINT32_MAX)
                return static_cast<uint32_t>(UINT32_MAX & ~mask);
            return static_cast<uint32_t>(aligned);
        }

        const uint32_t        m_baseAlignment;
        const uint32_t        m_capacity;
        uint8_t*              m_buffer;
        std::atomic<uint32_t> m_cursor{ 0 };
    };
}
