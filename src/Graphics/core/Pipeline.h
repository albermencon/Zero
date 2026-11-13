#pragma once
#include <cstddef>

namespace Zero
{
    class Pipeline
    {
    public:
        virtual ~Pipeline() = default;

        [[nodiscard]] virtual bool IsCompute() const noexcept = 0;

#if defined(ZERO_DEBUG) || defined(ZERO_ENABLE_PROFILING)
        [[nodiscard]] const char* GetDebugName() const noexcept { return m_debugName; }

    protected:
        void SetDebugName(const char* name, const char* fallback) noexcept
        {
            const char* src = (name && name[0] != '\0') ? name : fallback;
            size_t i = 0;
            if (src)
            {
                for (; i + 1 < sizeof(m_debugName) && src[i] != '\0'; ++i)
                    m_debugName[i] = src[i];
            }
            m_debugName[i] = '\0';
        }

    private:
        char m_debugName[48]{};
#endif
    };

    class GraphicsPipeline : public Pipeline
    {
    public:
        virtual ~GraphicsPipeline() override = default;

        [[nodiscard]] bool IsCompute() const noexcept override { return false; }
    };

    class ComputePipeline : public Pipeline
    {
    public:
        virtual ~ComputePipeline() override = default;

        [[nodiscard]] bool IsCompute() const noexcept override { return true; }
    };
}
