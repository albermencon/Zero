#pragma once
#include "Application.h"
#include "CrashReporter.h"

extern Zero::Application *Zero::CreateApplication();

int main(int argc, char **argv)
{
    Zero::CrashReporter::Init();
    auto app = Zero::CreateApplication();
    app->Run();
    delete app;
    Zero::CrashReporter::Shutdown();
    return 0;
}
