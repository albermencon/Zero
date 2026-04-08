#pragma once
#include <slang.h>
#include <slang-com-ptr.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace Zero
{
    enum class ShaderTarget
    {
        DXIL,
        SPIRV,
        GLSL,
        MSL
    };

    struct CompiledShader
    {
        std::vector<uint8_t> code;
        std::string targetName;
    };

    class SlangManager
    {
    public:
        static SlangManager& Get();

        CompiledShader Compile(const std::string& source, ShaderTarget target);

    private:
        SlangManager();
        ~SlangManager() = default;

        SlangManager(const SlangManager&) = delete;
        SlangManager& operator=(const SlangManager&) = delete;

    private:
        // Modern Slang API
        Slang::ComPtr<slang::IGlobalSession> m_globalSession;
        Slang::ComPtr<slang::ISession> m_session;

        // Target mapping
        std::unordered_map<ShaderTarget, SlangCompileTarget> m_targetMap;
        std::unordered_map<ShaderTarget, std::string> m_targetNameMap;
    };
}
