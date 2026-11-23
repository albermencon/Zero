#pragma once
#include <cstdint>
#include <Engine/Export.h>

namespace Zero
{
    // Deliberately left undefined
    template <typename T>
    struct ComponentTrait;

    // Compile-time duplicate-ID guard within any translation unit.
    template <uint32_t Id>
    struct ComponentIdSlot;

    namespace Detail
    {
        // Static-init duplicate-ID guard across translation units (no RTTI).
        ZERO_API const void* RegisterComponentIdToken(uint32_t id,
                                                      const void* typeToken,
                                                      const char* typeName) noexcept;
    }

    template<typename T>
    [[nodiscard]] inline constexpr uint32_t ComponentId() noexcept
    {
        return ComponentTrait<T>::kId;
    }
}

// Placed in component headers. Validates bounds at compile time and detects
// duplicate component IDs both within a TU (compile-time) and across TUs (static init).
#define REGISTER_COMPONENT(Type, StableId)                                          \
    namespace Zero {                                                                \
        template<> struct ComponentIdSlot<(StableId)> { using type = Type; };       \
        template<> struct ComponentTrait<Type> {                                    \
            static constexpr uint32_t kId = (StableId);                             \
            static_assert((StableId) < 128,                                         \
                #Type ": ECS Component ID must be in range [0, 127]");              \
            static inline const uint8_t kTypeTag = 0;                               \
            static inline const void* const kRegistered =                           \
                ::Zero::Detail::RegisterComponentIdToken(                           \
                    (StableId), &kTypeTag, #Type);                                  \
        };                                                                          \
    }
