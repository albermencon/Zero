#include "pch.h"
#include "ShaderCompiler.h"
#include <slang.h>
#include <slang-com-ptr.h>
#include <unordered_map>
#include <stdexcept>

namespace Zero
{
    using namespace slang;

    struct ShaderCompiler::Impl
    {
        Slang::ComPtr<slang::IGlobalSession> globalSession;
        Slang::ComPtr<slang::ISession> session;
        std::unordered_map<ShaderTarget, SlangCompileTarget> targetMap;
        std::unordered_map<ShaderTarget, std::string> targetNameMap;
        ShaderTarget currentTarget = ShaderTarget::SPIRV;
    };

    ShaderCompiler& ShaderCompiler::Get()
    {
        static ShaderCompiler instance;
        return instance;
    }

    ShaderCompiler::ShaderCompiler()
        : m_impl(std::make_unique<Impl>())
    {
        SlangResult res = createGlobalSession(m_impl->globalSession.writeRef());
        if (SLANG_FAILED(res) || !m_impl->globalSession)
            throw std::runtime_error("Failed to create Slang global session");

        m_impl->targetMap[ShaderTarget::DXIL]  = SLANG_DXIL;
        m_impl->targetMap[ShaderTarget::SPIRV] = SLANG_SPIRV;
        m_impl->targetMap[ShaderTarget::GLSL]  = SLANG_GLSL;
        m_impl->targetMap[ShaderTarget::MSL]   = SLANG_METAL;

        m_impl->targetNameMap[ShaderTarget::DXIL]  = "dxil";
        m_impl->targetNameMap[ShaderTarget::SPIRV] = "spirv";
        m_impl->targetNameMap[ShaderTarget::GLSL]  = "glsl";
        m_impl->targetNameMap[ShaderTarget::MSL]   = "metal";
    }

    ShaderCompiler::~ShaderCompiler() = default;

    void ShaderCompiler::Init(ShaderTarget target)
    {
        m_impl->currentTarget = target;

        auto it = m_impl->targetMap.find(target);
        if (it == m_impl->targetMap.end())
            throw std::runtime_error("Unknown shader target");

        const char* profileStr = "spirv_1_5";
        switch(target)
        {
            case ShaderTarget::SPIRV: profileStr = "spirv_1_5"; break;
            case ShaderTarget::DXIL:  profileStr = "sm_6_3"; break;
            case ShaderTarget::GLSL:  profileStr = "glsl_450"; break;
            case ShaderTarget::MSL:   profileStr = "msl_2_0"; break;
        }

        SessionDesc sessionDesc = {};
        TargetDesc targetDesc = {};
        targetDesc.format = it->second;
        targetDesc.profile = m_impl->globalSession->findProfile(profileStr);

        sessionDesc.targets = &targetDesc;
        sessionDesc.targetCount = 1;

        SlangResult res = m_impl->globalSession->createSession(sessionDesc, m_impl->session.writeRef());
        if (SLANG_FAILED(res) || !m_impl->session)
            throw std::runtime_error("Failed to create Slang session");
    }

    CompiledShader ShaderCompiler::Compile(const std::string& source, const char* entryPointName, ShaderStage stage, const std::vector<std::string>& searchPaths)
    {
        if (!m_impl->session)
            throw std::runtime_error("Shader compiler session not initialized. Call Init() first.");

        Slang::ComPtr<slang::ICompileRequest> request;
        SlangResult res = m_impl->session->createCompileRequest(request.writeRef());
        if (SLANG_FAILED(res) || !request)
            throw std::runtime_error("Failed to create compile request");

        int translationUnit = request->addTranslationUnit(
            SLANG_SOURCE_LANGUAGE_SLANG, nullptr);

        request->addTranslationUnitSourceString(
            translationUnit,
            "shader",
            source.c_str());

        SlangStage slangStage = SLANG_STAGE_NONE;
        switch(stage)
        {
            case ShaderStage::Vertex: slangStage = SLANG_STAGE_VERTEX; break;
            case ShaderStage::Fragment: slangStage = SLANG_STAGE_FRAGMENT; break;
            case ShaderStage::Compute: slangStage = SLANG_STAGE_COMPUTE; break;
        }

        request->addEntryPoint(translationUnit,
            entryPointName,
            slangStage);

        res = request->compile();
        if (SLANG_FAILED(res))
        {
            const char* diagnostics = request->getDiagnosticOutput();
            std::string error = diagnostics ? diagnostics : "Unknown shader compilation error";
            throw std::runtime_error(error);
        }

        Slang::ComPtr<ISlangBlob> blob;
        res = request->getEntryPointCodeBlob(0, 0, blob.writeRef());
        if (SLANG_FAILED(res) || !blob)
            throw std::runtime_error("Failed to retrieve compiled shader blob");

        CompiledShader out;
        out.targetName = m_impl->targetNameMap[m_impl->currentTarget];

        const uint8_t* data = reinterpret_cast<const uint8_t*>(blob->getBufferPointer());
        size_t size = blob->getBufferSize();

        out.code.assign(data, data + size);

        return out;
    }
}
