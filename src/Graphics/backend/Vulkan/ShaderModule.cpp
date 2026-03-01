#include "pch.h"
#include "ShaderModule.h"
#include <fstream>
#include <stdexcept>
#include <format>

namespace VoxelEngine {

    // ── SpirVSource ────────────────────────────────────────────────────────────

    SpirVSource SpirVSource::fromFile(std::string_view path) {
        // Open at end to get size in one stat-equivalent call
        std::ifstream file{ std::string(path), std::ios::ate | std::ios::binary };
        if (!file.is_open())
            throw std::runtime_error(std::format("ShaderModule: cannot open '{}'", path));

        SpirVSource src;
        src.owned.resize(static_cast<std::size_t>(file.tellg()));
        file.seekg(0, std::ios::beg);
        file.read(src.owned.data(), static_cast<std::streamsize>(src.owned.size()));
        src.view = src.owned;
        return src;
    }

    SpirVSource SpirVSource::fromEmbedded(std::span<const char> bytes) noexcept {
        SpirVSource src;
        src.view = bytes; // zero-copy — caller owns memory
        return src;
    }

    // ── ShaderModule ───────────────────────────────────────────────────────────

    ShaderModule::ShaderModule(const vk::raii::Device& device, std::string_view spvPath)
        : ShaderModule(device, SpirVSource::fromFile(spvPath)) {
    }

    ShaderModule::ShaderModule(const vk::raii::Device& device, std::span<const char> embeddedSpv)
        : ShaderModule(device, SpirVSource::fromEmbedded(embeddedSpv)) {
    }

    ShaderModule::ShaderModule(const vk::raii::Device& device, SpirVSource source)
        : m_source(std::move(source))
        , m_module(createModule(device, m_source.view)) {
    }

    vk::raii::ShaderModule ShaderModule::createModule(
        const vk::raii::Device& device, std::span<const char> code)
    {
        // SPIR-V size must be a multiple of 4 bytes
        if (code.size() % 4 != 0)
            throw std::runtime_error("ShaderModule: SPIR-V size not aligned to 4 bytes");

        vk::ShaderModuleCreateInfo ci{};
        ci.codeSize = code.size();
        ci.pCode = reinterpret_cast<const uint32_t*>(code.data());

        return vk::raii::ShaderModule{ device, ci };
    }

}
