#include "pch.h"
#include <Engine/Input/Input.h>
#include "Platform/InputInternal.h"
#include <cstring>

namespace VoxelEngine
{
    namespace Input
    {
        // 64 bytes = 512 bits, one bit per possible input.
        static uint8_t s_current[64] = {0};
        static uint8_t s_previous[64] = {0};

        // Mouse buttons occupy indices 400..407
        static constexpr int MOUSE_OFFSET = 400;

        static int MouseIndex(MouseButton btn)
        {
            return MOUSE_OFFSET + static_cast<int>(btn);
        }

        void SetKeyState(KeyCode key, bool pressed)
        {
            int idx = static_cast<int>(key);
            if (pressed)
                SetBit(s_current, idx);
            else
                ClearBit(s_current, idx);
        }

        void SetMouseButtonState(MouseButton button, bool pressed)
        {
            int idx = MOUSE_OFFSET + static_cast<int>(button);
            if (pressed)
                SetBit(s_current, idx);
            else
                ClearBit(s_current, idx);
        }

        void UpdateInput()
        {
            std::memcpy(s_previous, s_current, 64);
        }

        bool IsKeyPressed(KeyCode key)
        {
            int idx = static_cast<int>(key);
            int byte = idx >> 3; // idx / 8
            int bit = idx & 7;   // idx % 8
            return (s_current[byte] >> bit) & 1;
        }

        bool IsMouseButtonPressed(MouseButton button)
        {
            int idx = MouseIndex(button);
            int byte = idx >> 3;
            int bit = idx & 7;
            return (s_current[byte] >> bit) & 1;
        }

        bool IsKeyJustPressed(KeyCode key)
        {
            int idx = static_cast<int>(key);
            int byte = idx >> 3;
            int bit = idx & 7;
            return ((s_current[byte] >> bit) & 1) && !((s_previous[byte] >> bit) & 1);
        }

        bool IsKeyJustReleased(KeyCode key)
        {
            int idx = static_cast<int>(key);
            int byte = idx >> 3;
            int bit = idx & 7;
            return !((s_current[byte] >> bit) & 1) && ((s_previous[byte] >> bit) & 1);
        }

        bool IsMouseButtonJustPressed(MouseButton button)
        {
            int idx = MouseIndex(button);
            int byte = idx >> 3;
            int bit = idx & 7;
            return ((s_current[byte] >> bit) & 1) && !((s_previous[byte] >> bit) & 1);
        }

        bool IsMouseButtonJustReleased(MouseButton button)
        {
            int idx = MouseIndex(button);
            int byte = idx >> 3;
            int bit = idx & 7;
            return !((s_current[byte] >> bit) & 1) && ((s_previous[byte] >> bit) & 1);
        }

        std::pair<float, float> GetMousePosition()
        {
            // not supported yet
            return std::pair<float, float>(0.0, 0.0);
        }
    }
}
