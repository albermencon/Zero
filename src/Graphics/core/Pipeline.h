#pragma once

namespace Zero
{
    class Pipeline
    {
    public:
        virtual ~Pipeline() = default;

        [[nodiscard]] virtual bool IsCompute() const noexcept = 0;
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
