#pragma once
#include <Graphics/core/GraphicsDevice.h>

namespace Zero 
{
    enum class BackendType;
    class Window;

    class GraphicsBackend
    {
    public:
        static std::unique_ptr<GraphicsDevice> Create(BackendType type, Window& window);
    };
}
