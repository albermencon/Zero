#pragma once
#include <vector>

struct ImDrawData;
struct ImDrawList;

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
        [[nodiscard]] ImDrawData* GetNativeDrawData() const noexcept { return m_DrawData; }

    private:
        ImDrawData* m_DrawData = nullptr;
        std::vector<ImDrawList*> m_DrawListPool;
        size_t m_ActiveDrawLists = 0;
    };
}
