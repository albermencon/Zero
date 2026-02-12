#pragma once
#include "Application.h"

extern VoxelEngine::Application *VoxelEngine::CreateApplication();

int main(int argc, char **argv)
{
    auto app = VoxelEngine::CreateApplication();
    app->Run();
    delete app;
    return 0;
}
