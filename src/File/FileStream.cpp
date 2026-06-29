#include "pch.h"
#include <Engine/File/FileStream.h>
#include <system_error>

namespace Zero
{
    FileStream::FileStream(FileStream&& other) noexcept
        : m_Handle(other.m_Handle)
        , m_Cursor(other.m_Cursor)
    {
        other.m_Handle.handle = nullptr;
        other.m_Handle.size = 0;
        other.m_Cursor = 0;
    }

    FileStream& FileStream::operator=(FileStream&& other) noexcept
    {
        if (this != &other)
        {
            Close();
            m_Handle = other.m_Handle;
            m_Cursor = other.m_Cursor;
            other.m_Handle.handle = nullptr;
            other.m_Handle.size = 0;
            other.m_Cursor = 0;
        }
        return *this;
    }

    std::expected<FileStream, std::error_code> FileStream::Open(const std::filesystem::path& path, FileAccess access)
    {
        auto handle_res = FileSystem::Open(path, access);
        if (!handle_res)
        {
            return std::unexpected(handle_res.error());
        }

        FileStream stream;
        stream.m_Handle = *handle_res;
        stream.m_Cursor = 0;
        return stream;
    }

    void FileStream::Close()
    {
        if (m_Handle)
        {
            FileSystem::Close(m_Handle);
        }
        m_Cursor = 0;
    }

    void FileStream::Seek(size_t offset)
    {
        m_Cursor = offset;
    }

    std::expected<size_t, std::error_code> FileStream::Read(std::span<std::byte> destination)
    {
        auto res = FileSystem::Read(m_Handle, destination, m_Cursor);
        if (res)
        {
            m_Cursor += *res;
        }
        return res;
    }

    std::expected<size_t, std::error_code> FileStream::Write(std::span<const std::byte> source)
    {
        auto res = FileSystem::Write(m_Handle, source, m_Cursor);
        if (res)
        {
            m_Cursor += *res;
            if (m_Cursor > m_Handle.size)
            {
                m_Handle.size = m_Cursor;
            }
        }
        return res;
    }

    std::expected<std::string, std::error_code> FileStream::ReadString()
    {
        uint32_t length = 0;
        auto len_res = ReadValue(length);
        if (!len_res)
        {
            return std::unexpected(len_res.error());
        }

        std::string str;
        if (length > 0)
        {
            str.resize(length);
            auto data_span = std::span<std::byte>(reinterpret_cast<std::byte*>(str.data()), length);
            auto data_res = Read(data_span);
            if (!data_res)
            {
                return std::unexpected(data_res.error());
            }
            if (*data_res != length)
            {
                return std::unexpected(std::make_error_code(std::errc::value_too_large));
            }
        }
        return str;
    }

    std::expected<void, std::error_code> FileStream::WriteString(std::string_view str)
    {
        uint32_t length = static_cast<uint32_t>(str.size());
        auto len_res = WriteValue(length);
        if (!len_res)
        {
            return std::unexpected(len_res.error());
        }

        if (length > 0)
        {
            auto data_span = std::span<const std::byte>(reinterpret_cast<const std::byte*>(str.data()), length);
            auto data_res = Write(data_span);
            if (!data_res)
            {
                return std::unexpected(data_res.error());
            }
            if (*data_res != length)
            {
                return std::unexpected(std::make_error_code(std::errc::io_error));
            }
        }
        return {};
    }
}
