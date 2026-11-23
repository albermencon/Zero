#pragma once
#include <Engine/Ecs/ComponentTrait.h>
#include <type_traits>

namespace Zero
{
    struct TagComponent
    {
        uint64_t tagBits;   // bitmask — one bit per tag category
    };
    static_assert(std::is_trivially_copyable_v<TagComponent>);
}

REGISTER_COMPONENT(Zero::TagComponent, 6)
