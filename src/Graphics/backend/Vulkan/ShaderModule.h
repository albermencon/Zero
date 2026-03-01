#pragma once
#if defined(__INTELLISENSE__) || !defined(USE_CPP20_MODULES)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif
#include <span>
#include <string_view>
#include <vector>

namespace VoxelEngine {

    // Immutable SPIR-V source — either points to embedded bytes (zero-copy)
    // or owns a heap-allocated buffer loaded from disk.
    struct SpirVSource {
        std::vector<char>       owned;   // non-empty when loaded from file
        std::span<const char>   view;    // always valid after construction

        // Load from .spv file at runtime
        static SpirVSource fromFile(std::string_view path);

        // Wrap compile-time embedded bytes (e.g. via xxd / incbin)
        // Caller must ensure 'bytes' outlives this object.
        static SpirVSource fromEmbedded(std::span<const char> bytes) noexcept;
    };

    // Owns one vk::raii::ShaderModule.
    // Lightweight — create once, share via const ref or pointer.
    class ShaderModule {
    public:
        // Runtime path: throws std::runtime_error on I/O failure
        ShaderModule(const vk::raii::Device& device, std::string_view spvPath);

        // Embedded path: no I/O, no allocation
        ShaderModule(const vk::raii::Device& device, std::span<const char> embeddedSpv);

        // Common ctor used internally
        ShaderModule(const vk::raii::Device& device, SpirVSource source);

        [[nodiscard]] const vk::raii::ShaderModule& module() const noexcept { return m_module; }
        [[nodiscard]] vk::ShaderModule              handle() const noexcept { return *m_module; }

    private:
        SpirVSource            m_source; // keeps embedded lifetime trivially safe
        vk::raii::ShaderModule m_module;

        static vk::raii::ShaderModule createModule(
            const vk::raii::Device& device, std::span<const char> code);
    };

}
