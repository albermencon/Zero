#pragma once
#include <Engine/Ecs/ComponentTrait.h>
#include <cstring>
#include <type_traits>

namespace Zero
{
    // Fixed-size char array — trivially_copyable preserved.
    // Dynamic strings are not allowed in pool-resident component data.
    struct NameComponent
    {
        char name[32];

        // Convenience constructor — zero-fills remainder.
        explicit NameComponent(const char* str) noexcept
        {
            std::strncpy(name, str, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
        }
        NameComponent() noexcept { name[0] = '\0'; }
    };
    static_assert(std::is_trivially_copyable_v<NameComponent>);
}

REGISTER_COMPONENT(Zero::NameComponent, 7)
