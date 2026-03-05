#pragma once
#include <string>
#include "Core.h"
#include "Event.h"

namespace Zero
{
    class ENGINE_API Layer
    {
    public:
        explicit Layer(std::string name = "Layer")
            : m_DebugName(std::move(name))
        {
        }

        virtual ~Layer() = default;

        virtual void OnAttach() {}
        virtual void OnDetach() {}
        virtual void OnUpdate(float dt) {}
        virtual void OnRender() {}
        virtual void OnEvent(Event& event) {}

        const std::string& GetName() const noexcept
        {
            return m_DebugName;
        }

    protected:
        std::string m_DebugName;
    };
}
