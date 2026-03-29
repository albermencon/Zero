#pragma once
#include <memory>
#include <semaphore>
#include <readerwritercircularbuffer.h>
#include "Graphics/core/FrameData.h"

namespace Zero
{
    class FrameQueue
    {
    public:
        static constexpr size_t MAX_FRAMES_IN_FLIGHT = 3;

        FrameQueue(size_t frames_inflight = 3);

        void Push(FrameData* frame);

        // Non-blocking attempt. Returns false immediately if no slot available.
        [[nodiscard]] bool TryPush(FrameData* frame);

        // Dequeues one frame. Returns false if queue is empty (spin/yield caller).
        [[nodiscard]] bool Pop(FrameData*& out);

        // Must be called by RenderThread after it has finished consuming a frame
        // (after Present, when the FrameData can be safely destroyed or recycled).
        // Releases the backpressure slot so GameThread can produce the next frame.
        void FrameConsumed();

        bool TryAdquire();
        void Adquire();

    private:
        moodycamel::BlockingReaderWriterCircularBuffer<FrameData*> m_queue;
        std::counting_semaphore<MAX_FRAMES_IN_FLIGHT> m_availableSlots;
    };
}
