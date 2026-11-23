#pragma once
#include <Engine/Ecs/ComponentTrait.h>
#include <type_traits>

namespace Zero
{
    struct MaterialComponent
    {
        uint32_t materialId;
        uint32_t flags;
    };
    static_assert(std::is_trivially_copyable_v<MaterialComponent>);
}

REGISTER_COMPONENT(Zero::MaterialComponent, 5)
