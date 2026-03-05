#pragma once
#include "Engine/Core.h"
#include "Engine/Input/KeyCode.h"
#include "Engine/Input/MouseButton.h"
#include <utility>

namespace Zero
{
    namespace Input
    {
        ENGINE_API bool IsKeyPressed(KeyCode key);
        ENGINE_API bool IsMouseButtonPressed(MouseButton button);
        ENGINE_API bool IsKeyJustPressed(KeyCode key);
        ENGINE_API bool IsKeyJustReleased(KeyCode key);
        ENGINE_API bool IsMouseButtonJustPressed(MouseButton button);
        ENGINE_API bool IsMouseButtonJustReleased(MouseButton button);
        ENGINE_API std::pair<float, float> GetMousePosition();
    }
}
