#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace Zero
{
    enum class ShaderStage
    {
        Vertex,
        Fragment,
        Compute
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

        void Init(ShaderTarget target);

        CompiledShader Compile(const std::string& source, const char* entryPointName, ShaderStage stage, const std::vector<std::string>& searchPaths = {});

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
