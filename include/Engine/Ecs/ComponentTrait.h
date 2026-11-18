#pragma once
#include <cstdint>

namespace Zero 
{
    // Deliberately left undefined
    template <typename T>
    struct ComponentTrait;

    template<typename T>
    [[nodiscard]] inline constexpr uint32_t ComponentId() noexcept
    {
        return ComponentTrait<T>::kId;
    }
}

// Placed in component headers. Validates bounds strictly at compile time.
#define REGISTER_COMPONENT(Type, StableId)                               \
    namespace Zero {                                                \
        template<> struct ComponentTrait<Type> {                         \
            static constexpr uint32_t kId = (StableId);                  \
            static_assert((StableId) < 128,                              \
                #Type ": ECS Component ID must be in range [0, 127]");   \
        };                                                               \
    }
