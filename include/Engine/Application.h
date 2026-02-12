#pragma once
#include "Core.h"

namespace VoxelEngine
{
    class ENGINE_API Application
    {
    public:
        Application();
        virtual ~Application();

        void Run();
    };

    // To be defined in client
    Application *CreateApplication();
}
