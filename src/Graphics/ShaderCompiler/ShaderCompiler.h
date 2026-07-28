#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <cstdint>

namespace Zero
{
    enum class ShaderStage
    {
        Vertex,
        Fragment,
        Compute,
        Hull,
        Domain,
        Geometry
    };

    enum class ShaderTarget
    {
        DXIL,   // DirectX12
        SPIRV,  // Vulkan
        GLSL,   // OpenGL
        MSL     // Metal
    };

    struct CompiledShader
    {
        std::vector<uint8_t> code;
        std::string targetName;
    };

    class ShaderCompiler
    {
    public:
        static ShaderCompiler& Get();
        CompiledShader Compile(std::string_view source, ShaderTarget target, ShaderStage stage, std::string_view entryPoint = "main");

    private:
        ShaderCompiler();
        ~ShaderCompiler();

        ShaderCompiler(const ShaderCompiler&) = delete;
        ShaderCompiler& operator=(const ShaderCompiler&) = delete;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
