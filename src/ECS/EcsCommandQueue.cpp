#include "pch.h"
#include <Engine/Ecs/EcsCommandQueue.h>
#include <concurrentqueue.h>

namespace Zero
{
    struct EcsCommandQueue::Impl
    {
        moodycamel::ConcurrentQueue<EcsCommand> queue;
    };

    EcsCommandQueue::EcsCommandQueue(uint32_t slabBytes)
        : slab(slabBytes)
        , m_impl(std::make_unique<Impl>())
    {
    }

    EcsCommandQueue::~EcsCommandQueue()
    {
        if (!m_impl) return;

        EcsCommand cmd;
        while (m_impl->queue.try_dequeue(cmd))
        {
            if ((cmd.flags & kEcsFlagHeapPayload) && cmd.heapPtr)
            {
                ::operator delete(const_cast<void*>(cmd.heapPtr),
                                  std::align_val_t{ cmd.heapAlign });
            }
        }
    }

    void EcsCommandQueue::EnqueueCommand(const EcsCommand& cmd)
    {
        m_impl->queue.enqueue(cmd);
    }

    void EcsCommandQueue::EnqueueRemove(Entity entity, uint8_t componentId)
    {
        EcsCommand cmd{};
        cmd.type        = EcsCommandType::RemoveComponent;
        cmd.componentId = componentId;
        cmd.entity      = entity;
        m_impl->queue.enqueue(cmd);
    }

    void EcsCommandQueue::EnqueueDestroy(Entity entity)
    {
        EcsCommand cmd{};
        cmd.type   = EcsCommandType::DestroyEntity;
        cmd.entity = entity;
        m_impl->queue.enqueue(cmd);
    }

    bool EcsCommandQueue::TryDequeue(EcsCommand& outCmd)
    {
        return m_impl->queue.try_dequeue(outCmd);
    }
}
