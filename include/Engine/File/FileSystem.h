#pragma once
#include <chrono>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

namespace Zero
{
    enum class FileAccess : uint8_t
    {
        Read,
        Write,
        ReadWrite
    };

    struct FileHandle
    {
        void* handle = nullptr;
        size_t size = 0;

        explicit operator bool() const { return handle != nullptr; }
    };

    struct FileBuffer
    {
        std::vector<std::byte> data;
    };

    class FileSystem
    {
    public:
        static void Mount(std::string_view virtual_path, const std::filesystem::path& physical_path);
        static void Unmount(std::string_view virtual_path);
        static std::filesystem::path Resolve(std::string_view path);

        static bool Exists(const std::filesystem::path& path);
        static bool IsDirectory(const std::filesystem::path& path);
        static std::chrono::file_time<std::chrono::file_clock::duration> GetLastWriteTime(const std::filesystem::path& path);
        static bool Delete(const std::filesystem::path& path);

        static std::expected<FileHandle, std::error_code> Open(const std::filesystem::path& path, FileAccess access);
        static void Close(FileHandle& file);
        static std::expected<size_t, std::error_code> Read(FileHandle& file, std::span<std::byte> destination, size_t offset);
        static std::expected<size_t, std::error_code> Write(FileHandle& file, std::span<const std::byte> source, size_t offset);

        using DirectoryCallback = void(*)(const std::filesystem::directory_entry&, void*);
        static void IterateDirectory(const std::filesystem::path& path, DirectoryCallback callback, void* userData = nullptr);

        static std::expected<FileBuffer, std::error_code> ReadEntireFile(const std::filesystem::path& path);
        static std::expected<void, std::error_code> WriteEntireFile(const std::filesystem::path& path, std::span<const std::byte> buffer);
    };
}
