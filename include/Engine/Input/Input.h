#pragma once
#include "Engine/Core.h"
#include "Engine/Input/KeyCode.h"
#include "Engine/Input/MouseButton.h"
#include <utility>

namespace Zero
{
    namespace Input
    {
        ZERO_API bool IsKeyPressed(KeyCode key);
        ZERO_API bool IsMouseButtonPressed(MouseButton button);
        ZERO_API bool IsKeyJustPressed(KeyCode key);
        ZERO_API bool IsKeyJustReleased(KeyCode key);
        ZERO_API bool IsMouseButtonJustPressed(MouseButton button);
        ZERO_API bool IsMouseButtonJustReleased(MouseButton button);
        ZERO_API std::pair<float, float> GetMousePosition();
    }
}
