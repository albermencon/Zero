#include "pch.h"
#include "ShaderCompiler.h"
#include <Engine/Thread/ScopedLock.h>
#include <slang.h>
#include <slang-com-ptr.h>
#include <stdexcept>
#include <Engine/Thread/Mutex.h>
#include <array>

namespace Zero
{
    using namespace slang;

    constexpr SlangCompileTarget GetSlangTarget(ShaderTarget target)
    {
        switch (target)
        {
            case ShaderTarget::DXIL:  return SLANG_DXIL;
            case ShaderTarget::SPIRV: return SLANG_SPIRV;
            case ShaderTarget::GLSL:  return SLANG_GLSL;
            case ShaderTarget::MSL:   return SLANG_METAL;
        }
        std::unreachable();
    }

    constexpr const char* GetTargetProfile(ShaderTarget target)
    {
        switch (target)
        {
            case ShaderTarget::DXIL:  return "sm_6_3";
            case ShaderTarget::SPIRV: return "spirv_1_5";
            case ShaderTarget::GLSL:  return "glsl_450";
            case ShaderTarget::MSL:   return "msl_2_0";
        }
        std::unreachable();
    }

    constexpr const char* GetTargetName(ShaderTarget target)
    {
        switch (target)
        {
            case ShaderTarget::DXIL:  return "dxil";
            case ShaderTarget::SPIRV: return "spirv";
            case ShaderTarget::GLSL:  return "glsl";
            case ShaderTarget::MSL:   return "metal";
        }
        std::unreachable();
    }

    constexpr SlangStage GetSlangStage(ShaderStage stage)
    {
        switch (stage)
        {
            case ShaderStage::Vertex:   return SLANG_STAGE_VERTEX;
            case ShaderStage::Fragment: return SLANG_STAGE_FRAGMENT;
            case ShaderStage::Compute:  return SLANG_STAGE_COMPUTE;
            case ShaderStage::Hull:     return SLANG_STAGE_HULL;
            case ShaderStage::Domain:   return SLANG_STAGE_DOMAIN;
            case ShaderStage::Geometry: return SLANG_STAGE_GEOMETRY;
        }
        std::unreachable();
    }

    struct ShaderCompiler::Impl
    {
        Slang::ComPtr<slang::IGlobalSession> globalSession;
        std::array<Slang::ComPtr<slang::ISession>, 4> sessionCache;
        Zero::Mutex sessionMutex;

        slang::ISession* GetOrCreateSession(ShaderTarget target)
        {
            size_t idx = static_cast<size_t>(target);
            
            Zero::ScopedLock<Zero::Mutex> lock(sessionMutex);
            if (sessionCache[idx])
                return sessionCache[idx].get();

            SessionDesc sessionDesc = {};
            TargetDesc targetDesc = {};
            targetDesc.format = GetSlangTarget(target);
            targetDesc.profile = globalSession->findProfile(GetTargetProfile(target));

            sessionDesc.targets = &targetDesc;
            sessionDesc.targetCount = 1;

            SlangResult res = globalSession->createSession(sessionDesc, sessionCache[idx].writeRef());
            if (SLANG_FAILED(res) || !sessionCache[idx])
                throw std::runtime_error("Failed to create Slang session");

            return sessionCache[idx].get();
        }
    };

    ShaderCompiler& ShaderCompiler::Get()
    {
        static ShaderCompiler instance;
        return instance;
    }

    ShaderCompiler::ShaderCompiler() : m_impl(std::make_unique<Impl>())
    {
        SlangResult res = createGlobalSession(m_impl->globalSession.writeRef());
        if (SLANG_FAILED(res) || !m_impl->globalSession)
            throw std::runtime_error("Failed to create Slang global session");
    }

    ShaderCompiler::~ShaderCompiler() = default;

    CompiledShader ShaderCompiler::Compile(std::string_view source, ShaderTarget target, ShaderStage stage, std::string_view entryPoint)
    {
        slang::ISession* session = m_impl->GetOrCreateSession(target);

        Slang::ComPtr<slang::ICompileRequest> request;
        SlangResult res = session->createCompileRequest(request.writeRef());
        if (SLANG_FAILED(res) || !request)
            throw std::runtime_error("Failed to create compile request");

        int translationUnit = request->addTranslationUnit(SLANG_SOURCE_LANGUAGE_SLANG, nullptr);


        request->addTranslationUnitSourceString(translationUnit, "shader", source.data());
        request->addEntryPoint(translationUnit, entryPoint.data(), GetSlangStage(stage));

        res = request->compile();
        if (SLANG_FAILED(res))
        {
            const char* diagnostics = request->getDiagnosticOutput();
            throw std::runtime_error(diagnostics ? diagnostics : "Unknown shader compilation error");
        }

        Slang::ComPtr<ISlangBlob> blob;
        res = request->getEntryPointCodeBlob(0, 0, blob.writeRef());
        if (SLANG_FAILED(res) || !blob)
            throw std::runtime_error("Failed to retrieve compiled shader blob");

        CompiledShader out;
        out.targetName = GetTargetName(target);

        const uint8_t* data = reinterpret_cast<const uint8_t*>(blob->getBufferPointer());
        out.code.assign(data, data + blob->getBufferSize());

        return out;
    }
}
