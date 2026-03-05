#pragma once
#include <Engine/Layer.h>
#include <memory>
#include <vector>

namespace Zero
{
    class LayerStack
    {
    public:
        LayerStack();
        ~LayerStack();

        Layer *PushLayer(std::unique_ptr<Layer> layer);
        Layer *PushOverlay(std::unique_ptr<Layer> overlay);

        void PopLayer(Layer *layer);

        size_t Size() const noexcept
        {
            return m_Layers.size();
        }

        // Iterators
        auto begin() { return m_Layers.begin(); }
        auto end() { return m_Layers.end(); }
        auto rbegin() { return m_Layers.rbegin(); }
        auto rend() { return m_Layers.rend(); }

        auto begin() const { return m_Layers.begin(); }
        auto end() const { return m_Layers.end(); }
        auto rbegin() const { return m_Layers.rbegin(); }
        auto rend() const { return m_Layers.rend(); }

    private:
        std::vector<std::unique_ptr<Layer>> m_Layers;
        unsigned int m_LayerInsertIndex = 0;
    };
}
