#include "pch.h"
#include <Engine/EventDispatcher.h>

namespace VoxelEngine
{
    EventDispatcher::EventDispatcher(Event &event)
        : m_Event(event) {}
}
