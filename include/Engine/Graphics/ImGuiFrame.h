#pragma once
#include <vector>
#include <imgui.h>

namespace Zero
{
    class ImGuiFrame
    {
    public:
        ImGuiFrame();
        ~ImGuiFrame();

        ImGuiFrame(const ImGuiFrame&) = delete;
        ImGuiFrame& operator=(const ImGuiFrame&) = delete;
        ImGuiFrame(ImGuiFrame&&) noexcept;
        ImGuiFrame& operator=(ImGuiFrame&&) noexcept;

        void CopyFrom(ImDrawData* src);
        void Reset();

    private:
        friend class VulkanDevice;
        friend class OpenGLDevice;
        [[nodiscard]] ImDrawData* GetNativeDrawData() noexcept { return m_DrawData.Valid ? &m_DrawData : nullptr; }

    private:
        ImDrawData m_DrawData{};
        std::vector<ImDrawList*> m_DrawListPool;
        size_t m_ActiveDrawLists = 0;
    };
}
