#pragma once
#include <Engine/Ecs/ComponentTrait.h>
#include <type_traits>

namespace Zero
{
    struct RenderableHandleComponent
    {
        uint32_t id;
    };
    static_assert(std::is_trivially_copyable_v<RenderableHandleComponent>);
}

REGISTER_COMPONENT(Zero::RenderableHandleComponent, 8)
