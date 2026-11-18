#pragma once
#include "ComponentMask.h"
#include "ComponentTrait.h"

namespace Zero
{
    template<typename... Ts>
    [[nodiscard]] constexpr ComponentMask MaskOf() noexcept
    {
        ComponentMask m;
        (m.Set(ComponentId<Ts>()), ...);
        return m;
    }
}
