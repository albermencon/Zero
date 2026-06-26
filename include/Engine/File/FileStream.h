#pragma once
#include "FileSystem.h"
#include <string>
#include <type_traits>

namespace Zero
{
    class FileStream
    {
    public:
        FileStream() = default;
        ~FileStream() { Close(); }

        FileStream(const FileStream&) = delete;
        FileStream& operator=(const FileStream&) = delete;

        FileStream(FileStream&& other) noexcept;
        FileStream& operator=(FileStream&& other) noexcept;

        static std::expected<FileStream, std::error_code> Open(const std::filesystem::path& path, FileAccess access);
        void Close();

        bool IsOpen() const { return m_Handle.operator bool(); }
        size_t GetSize() const { return m_Handle.size; }
        size_t GetPosition() const { return m_Cursor; }
        void Seek(size_t offset);

        std::expected<size_t, std::error_code> Read(std::span<std::byte> destination);
        std::expected<size_t, std::error_code> Write(std::span<const std::byte> source);

        template<typename T>
        std::expected<void, std::error_code> ReadValue(T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            auto result = Read(std::span<std::byte>(reinterpret_cast<std::byte*>(&value), sizeof(T)));
            if (!result) return std::unexpected(result.error());
            if (*result != sizeof(T)) return std::unexpected(std::make_error_code(std::errc::value_too_large));
            return {};
        }

        template<typename T>
        std::expected<void, std::error_code> WriteValue(const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            auto result = Write(std::span<const std::byte>(reinterpret_cast<const std::byte*>(&value), sizeof(T)));
            if (!result) return std::unexpected(result.error());
            return {};
        }

        std::expected<std::string, std::error_code> ReadString();
        std::expected<void, std::error_code> WriteString(std::string_view str);

    private:
        FileHandle m_Handle;
        size_t m_Cursor = 0;
    };
}
