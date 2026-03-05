#pragma once
#include <Engine/Input/KeyCode.h>
#include <Engine/Input/MouseButton.h>
#include <cstdint>

namespace Zero
{
    namespace Input
    {
        void SetKeyState(KeyCode key, bool pressed);
        void SetMouseButtonState(MouseButton button, bool pressed);

        void UpdateInput();

        static inline void SetBit(uint8_t state[64], int idx)
        {
            state[idx >> 3] |= (1u << (idx & 7));
        }

        static inline void ClearBit(uint8_t state[64], int idx)
        {
            state[idx >> 3] &= ~(1u << (idx & 7));
        }
    }
}
