#include "pch.h"
#include "LayerStack.h"
#include <algorithm>
#include <memory>

namespace Zero
{
    LayerStack::LayerStack() = default;

    LayerStack::~LayerStack()
    {
        for (auto &layer : m_Layers)
        {
            layer->OnDetach();
        }
    }

    Layer *LayerStack::PushLayer(std::unique_ptr<Layer> layer)
    {
        Layer *raw = layer.get();
        m_Layers.emplace(m_Layers.begin() + m_LayerInsertIndex, std::move(layer));
        m_LayerInsertIndex++;
        return raw;
    }

    Layer *LayerStack::PushOverlay(std::unique_ptr<Layer> overlay)
    {
        Layer *raw = overlay.get();
        m_Layers.emplace_back(std::move(overlay));
        return raw;
    }

    void LayerStack::PopLayer(Layer *layer)
    {
        auto it = std::find_if(m_Layers.begin(), m_Layers.begin() + m_LayerInsertIndex,
                               [layer](const std::unique_ptr<Layer> &ptr)
                               {
                                   return ptr.get() == layer;
                               });

        if (it != m_Layers.begin() + m_LayerInsertIndex)
        {
            (*it)->OnDetach();
            m_Layers.erase(it);
            m_LayerInsertIndex--;
        }
    }
}
