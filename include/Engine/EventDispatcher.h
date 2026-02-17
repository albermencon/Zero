#pragma once
#include "Core.h"
#include "Event.h"

namespace VoxelEngine
{
    class ENGINE_API EventDispatcher
    {
    public:
        EventDispatcher(Event &event);

        // F will be deduced by the compiler
        template <typename T, typename F>
        bool Dispatch(const F &func)
        {
            if (m_Event.GetEventType() == T::GetStaticType())
            {
                // Call the function, handling the cast
                m_Event.Handled |= func(static_cast<T &>(m_Event));
                return true;
            }
            return false;
        }

    private:
        Event &m_Event;
    };
}
