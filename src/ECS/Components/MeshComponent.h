#pragma once
#include <Engine/Ecs/ComponentTrait.h>
#include <type_traits>

namespace Zero
{
    struct MeshComponent
    {
        uint32_t meshId;
        uint32_t materialId;
    };
    static_assert(std::is_trivially_copyable_v<MeshComponent>);
}

REGISTER_COMPONENT(Zero::MeshComponent, 2)
