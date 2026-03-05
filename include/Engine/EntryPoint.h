#pragma once
#include "Application.h"

extern Zero::Application *Zero::CreateApplication();

int main(int argc, char **argv)
{
    auto app = Zero::CreateApplication();
    app->Run();
    delete app;
    return 0;
}
