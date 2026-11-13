#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include <Engine/Log.h>

int main(int argc, char** argv)
{
    Zero::Log::SetEnabled(false);

    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}

