#pragma once
#include <cstdint>

namespace Zero
{
    class ZERO_API Time
    {
    public:
        static void Init();
        static void Update();

        static float GetTime();      // Seconds since engine start
        static float GetDeltaTime(); // Seconds between frames
        static uint64_t GetTimeMs();    // Miliseconds since engine start
        static uint64_t GetDeltaTimeMs(); // Miliseconds between frames

    private:
        static double s_StartTime;
        static double s_CurrentTime;
        static double s_DeltaTime;
    };
}
