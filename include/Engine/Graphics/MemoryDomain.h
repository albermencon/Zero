#pragma once
#include <Engine/Core.h>
#include <cstdint>

namespace Zero 
{
    enum class ENGINE_API MemoryDomain : uint8_t
    {
        GPU,                // device-local, no CPU access —- fastest for GPU reads
        CPUtoGPU,           // upload heap, persistently mappable (UBOs, streaming)
        GPUtoCPU,           // readback heap, cached —- for GPU to CPU transfers
        CPUtoGPU_Coherent   // persistently mapped, coherent (DX12: UPLOAD heap)
    };
}
