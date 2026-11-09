#pragma once

namespace Zero
{
    class Pipeline
    {
    public:
        virtual ~Pipeline() = default;

        [[nodiscard]] virtual bool IsCompute() const noexcept = 0;
    };
}
