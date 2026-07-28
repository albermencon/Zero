#pragma once
#include <Engine/Graphics/PipelineDesc.h>

namespace Zero::EmbeddedShaders
{
    static constexpr const char* DefaultTriangleShader = R"(
struct VSOutput
{
    float4 position : SV_Position;
    float3 color    : COLOR;
};

[shader("vertex")]
VSOutput vertMain(uint vertexID : SV_VertexID)
{
    float2 positions[3] = {
        float2(0.0, 0.5),
        float2(-0.5, -0.5),
        float2(0.5, -0.5)
    };
    float3 colors[3] = {
        float3(1.0, 0.0, 0.0),
        float3(0.0, 1.0, 0.0),
        float3(0.0, 0.0, 1.0)
    };

    VSOutput output;
    output.position = float4(positions[vertexID], 0.0, 1.0);
    output.color = colors[vertexID];
    return output;
}

[shader("fragment")]
float4 fragMain(VSOutput input) : SV_Target
{
    return float4(input.color, 1.0);
}
)";

    [[nodiscard]] inline PipelineDesc DefaultTrianglePipeline()
    {
        PipelineDesc d;
        d.debugName = "Default Embedded Triangle";
        d.shaders.vertex.path = DefaultTriangleShader;
        d.shaders.vertex.entryPoint = "vertMain";
        d.shaders.fragment.path = DefaultTriangleShader;
        d.shaders.fragment.entryPoint = "fragMain";
        d.rasterizerState = RasterizerState::Default();
        d.depthStencilState = DepthStencilState::Disabled();
        d.blendState = BlendState::Opaque();
        return d;
    }
}
