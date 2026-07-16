#include "pch.h"
#include "Engine/Graphics/ImGuiFrame.h"
#include <imgui.h>

namespace Zero
{
    ImGuiFrame::ImGuiFrame() = default;

    ImGuiFrame::ImGuiFrame(ImGuiFrame&& other) noexcept
        : m_DrawData(other.m_DrawData),
          m_DrawListPool(std::move(other.m_DrawListPool)),
          m_ActiveDrawLists(other.m_ActiveDrawLists)
    {
        other.m_DrawData = nullptr;
        other.m_ActiveDrawLists = 0;
    }

    ImGuiFrame& ImGuiFrame::operator=(ImGuiFrame&& other) noexcept
    {
        if (this != &other)
        {
            delete m_DrawData;
            for (ImDrawList* list : m_DrawListPool)
                delete list;
            
            m_DrawData = other.m_DrawData;
            m_DrawListPool = std::move(other.m_DrawListPool);
            m_ActiveDrawLists = other.m_ActiveDrawLists;

            other.m_DrawData = nullptr;
            other.m_ActiveDrawLists = 0;
        }
        return *this;
    }

    ImGuiFrame::~ImGuiFrame()
    {
        for (ImDrawList* list : m_DrawListPool)
        {
            delete list;
        }
        m_DrawListPool.clear();
        delete m_DrawData;
    }

    void ImGuiFrame::CopyFrom(ImDrawData* src)
    {
        if (!src || src->CmdListsCount == 0)
        {
            Reset();
            return;
        }

        m_ActiveDrawLists = src->CmdListsCount;

        if (m_DrawListPool.size() < m_ActiveDrawLists)
        {
            m_DrawListPool.reserve(m_ActiveDrawLists);
            while (m_DrawListPool.size() < m_ActiveDrawLists)
            {
                m_DrawListPool.push_back(new ImDrawList(src->CmdLists[0]->_Data));
            }
        }

        for (int i = 0; i < src->CmdListsCount; ++i)
        {
            ImDrawList* dst = m_DrawListPool[i];
            ImDrawList* source = src->CmdLists[i];

            dst->CmdBuffer = source->CmdBuffer;
            dst->VtxBuffer = source->VtxBuffer;
            dst->IdxBuffer = source->IdxBuffer;
            dst->Flags     = source->Flags;
        }

        if (!m_DrawData) m_DrawData = new ImDrawData();
        *m_DrawData = *src;
        m_DrawData->CmdLists.resize(src->CmdListsCount);
        for (int i = 0; i < src->CmdListsCount; ++i)
        {
            m_DrawData->CmdLists[i] = m_DrawListPool[i];
        }
    }

    void ImGuiFrame::Reset()
    {
        m_ActiveDrawLists = 0;
        if (m_DrawData)
        {
            m_DrawData->CmdListsCount = 0;
            m_DrawData->TotalVtxCount = 0;
            m_DrawData->TotalIdxCount = 0;
            m_DrawData->CmdLists.resize(0);
        }
    }
}
