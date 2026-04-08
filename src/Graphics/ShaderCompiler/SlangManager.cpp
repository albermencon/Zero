#include "pch.h"
#include "SlangManager.h"
#include <stdexcept>

namespace Zero
{
    using namespace slang;

    SlangManager& SlangManager::Get()
    {
        static SlangManager instance;
        return instance;
    }

    SlangManager::SlangManager()
    {
        // Create global session
        SlangResult res = createGlobalSession(m_globalSession.writeRef());
        if (SLANG_FAILED(res) || !m_globalSession)
            throw std::runtime_error("Failed to create Slang global session");

        // Map targets to Slang enums
        m_targetMap[ShaderTarget::DXIL]  = SLANG_DXIL;
        m_targetMap[ShaderTarget::SPIRV] = SLANG_SPIRV;
        m_targetMap[ShaderTarget::GLSL]  = SLANG_GLSL;
        m_targetMap[ShaderTarget::MSL]   = SLANG_METAL;

        m_targetNameMap[ShaderTarget::DXIL]  = "dxil";
        m_targetNameMap[ShaderTarget::SPIRV] = "spirv";
        m_targetNameMap[ShaderTarget::GLSL]  = "glsl";
        m_targetNameMap[ShaderTarget::MSL]   = "metal";
    }

    CompiledShader SlangManager::Compile(const std::string& source, ShaderTarget target)
    {
        if (!m_globalSession)
            throw std::runtime_error("Slang global session not initialized");

        // Set target
        auto it = m_targetMap.find(target);
        if (it == m_targetMap.end())
            throw std::runtime_error("Unknown shader target");

        // Create session
        SessionDesc sessionDesc = {};
        TargetDesc targetDesc = {};
        targetDesc.format = it->second;
        targetDesc.profile = m_globalSession->findProfile("spirv_1_5");

        sessionDesc.targets = &targetDesc;
        sessionDesc.targetCount = 1;

        Slang::ComPtr<slang::ISession> session;

        SlangResult res = m_globalSession->createSession(sessionDesc, session.writeRef());
        if (SLANG_FAILED(res) || !session)
            throw std::runtime_error("Failed to create Slang session");

        // Create compile request
        Slang::ComPtr<slang::ICompileRequest> request;
        res = session->createCompileRequest(request.writeRef());
        if (SLANG_FAILED(res) || !request)
            throw std::runtime_error("Failed to create compile request");

        // Add translation unit
        int translationUnit = request->addTranslationUnit(
            SLANG_SOURCE_LANGUAGE_SLANG, nullptr);

        request->addTranslationUnitSourceString(
            translationUnit,
            "shader",
            source.c_str());

        request->addEntryPoint(translationUnit,
            "vertMain",
        SLANG_STAGE_VERTEX);

        // Compile
        res = request->compile();
        if (SLANG_FAILED(res))
        {
            const char* diagnostics = request->getDiagnosticOutput();
            std::string error = diagnostics ? diagnostics : "Unknown Slang error";
            throw std::runtime_error(error);
        }

        // Extract output
        Slang::ComPtr<ISlangBlob> blob;
        res = request->getEntryPointCodeBlob(0, 0, blob.writeRef());
        if (SLANG_FAILED(res) || !blob)
            throw std::runtime_error("Failed to retrieve compiled shader blob");

        CompiledShader out;
        out.targetName = m_targetNameMap[target];

        const uint8_t* data = reinterpret_cast<const uint8_t*>(blob->getBufferPointer());
        size_t size = blob->getBufferSize();

        out.code.assign(data, data + size);

        return out;
    }
}
