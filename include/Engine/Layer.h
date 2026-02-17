#pragma once
#include <string>
#include "Core.h"
#include "Event.h"

namespace VoxelEngine
{
    class ENGINE_API Layer
    {
    public:
#ifndef ENGINE_DEBUG
        Layer(const std::string &name = "Layer")
            : m_DebugName(name) {}
#else
        Layer(const std::string & = "")
        {
        }
#endif

        virtual ~Layer() = default;

        virtual void OnAttach() {}
        virtual void OnDetach() {}
        virtual void OnUpdate(float dt) {}
        virtual void OnRender() {}
        virtual void OnEvent(Event &event) {}

#ifndef ENGINE_DEBUG
        const std::string &GetName() const { return m_DebugName; }
#else
        const char *GetName() const { return ""; }
#endif

    protected:
#ifndef NDEBUG
        std::string m_DebugName;
#endif
    };
}
