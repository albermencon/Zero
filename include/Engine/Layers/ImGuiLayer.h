#pragma once
#include "Engine/Layer.h"

namespace Zero
{
    class FrameData;

    class ZERO_API ImGuiLayer : public Layer
    {
    public:
        ImGuiLayer();
        ~ImGuiLayer();

        virtual void OnAttach() override;
        virtual void OnDetach() override;
        virtual void OnEvent(Event& event) override;
        
        void Begin();
        void End(FrameData* currentFrame);
    };
}
