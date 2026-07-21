#include "pch.h"
#include "ShaderProgram.h"

namespace Zero 
{

    ShaderStage ShaderStage::vertex(const ShaderModule& m, const char* ep) noexcept {
        return { &m, vk::ShaderStageFlagBits::eVertex, ep };
    }
    ShaderStage ShaderStage::fragment(const ShaderModule& m, const char* ep) noexcept {
        return { &m, vk::ShaderStageFlagBits::eFragment, ep };
    }
    ShaderStage ShaderStage::compute(const ShaderModule& m, const char* ep) noexcept {
        return { &m, vk::ShaderStageFlagBits::eCompute, ep };
    }

    void ShaderProgram::addStage(ShaderStage stage) {
        m_stages.push_back(stage);
        rebuildInfos();
    }

    std::span<const vk::PipelineShaderStageCreateInfo>
        ShaderProgram::stageInfos() const noexcept {
        return m_stageInfos;
    }

    ShaderProgram ShaderProgram::fromFiles(
        const vk::raii::Device& device,
        std::string_view vertSpv,
        std::string_view fragSpv)
    {
        ShaderProgram prog;
        prog.m_modules.emplace_back(device, vertSpv);
        prog.m_modules.emplace_back(device, fragSpv);
        prog.m_stages.push_back(ShaderStage::vertex(prog.m_modules[0]));
        prog.m_stages.push_back(ShaderStage::fragment(prog.m_modules[1]));
        prog.rebuildInfos();
        return prog;
    }

    ShaderProgram ShaderProgram::fromEmbedded(
        const vk::raii::Device& device,
        std::span<const char> vertSpv,
        std::span<const char> fragSpv)
    {
        ShaderProgram prog;
        prog.m_modules.emplace_back(device, vertSpv);
        prog.m_modules.emplace_back(device, fragSpv);
        prog.m_stages.push_back(ShaderStage::vertex(prog.m_modules[0]));
        prog.m_stages.push_back(ShaderStage::fragment(prog.m_modules[1]));
        prog.rebuildInfos();
        return prog;
    }

    void ShaderProgram::rebuildInfos() {
        m_stageInfos.clear();
        m_stageInfos.reserve(m_stages.size());
        for (const auto& s : m_stages) {
            vk::PipelineShaderStageCreateInfo info{};
            info.stage = s.stage;
            info.module = s.module->handle();
            info.pName = s.entryPoint;

            m_stageInfos.push_back(info);
        }
    }

}
