#include "pch.h"
#include "Engine/Graphics/ImGuiFrame.h"
#include <imgui.h>

namespace Zero
{
    ImGuiFrame::ImGuiFrame() = default;

    ImGuiFrame::ImGuiFrame(ImGuiFrame&& other) noexcept
        : m_DrawData(std::move(other.m_DrawData)),
          m_DrawListPool(std::move(other.m_DrawListPool)),
          m_ActiveDrawLists(other.m_ActiveDrawLists)
    {
        other.m_ActiveDrawLists = 0;
    }

    ImGuiFrame& ImGuiFrame::operator=(ImGuiFrame&& other) noexcept
    {
        if (this != &other)
        {
            for (ImDrawList* list : m_DrawListPool)
                delete list;
            
            m_DrawData = std::move(other.m_DrawData);
            m_DrawListPool = std::move(other.m_DrawListPool);
            m_ActiveDrawLists = other.m_ActiveDrawLists;

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
                m_DrawListPool.push_back(new ImDrawList(nullptr));
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

        m_DrawData.Valid            = src->Valid;
        m_DrawData.CmdListsCount    = src->CmdListsCount;
        m_DrawData.TotalIdxCount    = src->TotalIdxCount;
        m_DrawData.TotalVtxCount    = src->TotalVtxCount;
        m_DrawData.DisplayPos       = src->DisplayPos;
        m_DrawData.DisplaySize      = src->DisplaySize;
        m_DrawData.FramebufferScale = src->FramebufferScale;
        m_DrawData.OwnerViewport    = src->OwnerViewport;
        m_DrawData.Textures         = src->Textures;
        m_DrawData.CmdLists.resize(src->CmdListsCount);
        for (int i = 0; i < src->CmdListsCount; ++i)
        {
            m_DrawData.CmdLists[i] = m_DrawListPool[i];
        }
    }

    void ImGuiFrame::Reset()
    {
        m_ActiveDrawLists = 0;
        m_DrawData.Valid = false;
        m_DrawData.CmdListsCount = 0;
        m_DrawData.TotalVtxCount = 0;
        m_DrawData.TotalIdxCount = 0;
        m_DrawData.Textures = nullptr;
        m_DrawData.CmdLists.clear();
    }
}
