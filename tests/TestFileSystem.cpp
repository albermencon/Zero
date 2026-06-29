#include <doctest.h>
#include <Engine/File/FileSystem.h>
#include <filesystem>
#include <string_view>
#include <vector>
#include <system_error>

using namespace Zero;

TEST_CASE("FileSystem: Mount, Unmount and Resolve")
{
    FileSystem::Mount("virtual_test", "physical_test/dir");
    std::filesystem::path resolved = FileSystem::Resolve("virtual_test/file.txt");
    CHECK(resolved.generic_string() == "physical_test/dir/file.txt");

    resolved = FileSystem::Resolve("virtual_test_suffix/file.txt");
    CHECK(resolved.generic_string() == "virtual_test_suffix/file.txt");

    FileSystem::Unmount("virtual_test");
    resolved = FileSystem::Resolve("virtual_test/file.txt");
    CHECK(resolved.generic_string() == "virtual_test/file.txt");
}

TEST_CASE("FileSystem: File Operations")
{
    std::filesystem::path test_path = "fs_test_temp_file.dat";
    if (std::filesystem::exists(test_path))
    {
        FileSystem::Delete(test_path);
    }

    std::string test_data = "Hello Systems Engine";
    auto data_span = std::span<const std::byte>(reinterpret_cast<const std::byte*>(test_data.data()), test_data.size());

    auto write_res = FileSystem::WriteEntireFile(test_path, data_span);
    REQUIRE(write_res.has_value());

    CHECK(FileSystem::Exists(test_path));
    CHECK(!FileSystem::IsDirectory(test_path));

    auto time = FileSystem::GetLastWriteTime(test_path);
    CHECK(time.time_since_epoch().count() > 0);

    auto read_res = FileSystem::ReadEntireFile(test_path);
    REQUIRE(read_res.has_value());
    CHECK(read_res->data.size() == test_data.size());
    std::string read_str(reinterpret_cast<const char*>(read_res->data.data()), read_res->data.size());
    CHECK(read_str == test_data);

    auto file_res = FileSystem::Open(test_path, FileAccess::ReadWrite);
    REQUIRE(file_res.has_value());
    FileHandle& handle = *file_res;
    CHECK(handle.size == test_data.size());

    std::string overwrite_str = "Zero";
    auto overwrite_span = std::span<const std::byte>(reinterpret_cast<const std::byte*>(overwrite_str.data()), overwrite_str.size());
    auto overwrite_res = FileSystem::Write(handle, overwrite_span, 6);
    REQUIRE(overwrite_res.has_value());
    CHECK(*overwrite_res == overwrite_str.size());

    std::vector<std::byte> read_part(4);
    auto read_part_res = FileSystem::Read(handle, std::span<std::byte>(read_part.data(), read_part.size()), 6);
    REQUIRE(read_part_res.has_value());
    CHECK(*read_part_res == 4);
    std::string read_part_str(reinterpret_cast<const char*>(read_part.data()), read_part.size());
    CHECK(read_part_str == overwrite_str);

    FileSystem::Close(handle);

    struct DirectoryEntryCollector
    {
        std::filesystem::path target;
        bool found = false;
        static void Collect(const std::filesystem::directory_entry& entry, void* userData)
        {
            auto* self = static_cast<DirectoryEntryCollector*>(userData);
            if (entry.path().filename() == self->target)
            {
                self->found = true;
            }
        }
    };

    DirectoryEntryCollector collector;
    collector.target = test_path;
    FileSystem::IterateDirectory(".", DirectoryEntryCollector::Collect, &collector);
    CHECK(collector.found);

    FileSystem::Delete(test_path);
}

TEST_CASE("FileSystem: Edge Cases and Errors")
{
    std::filesystem::path missing_path = "does_not_exist_xyz.dat";
    CHECK(!FileSystem::Exists(missing_path));
    CHECK(!FileSystem::IsDirectory(missing_path));

    auto time = FileSystem::GetLastWriteTime(missing_path);
    CHECK(time.time_since_epoch().count() == 0);

    auto open_read_res = FileSystem::Open(missing_path, FileAccess::Read);
    CHECK(!open_read_res.has_value());

    FileHandle invalid_handle;
    std::vector<std::byte> buf(10);
    auto read_res = FileSystem::Read(invalid_handle, std::span<std::byte>(buf.data(), buf.size()), 0);
    CHECK(!read_res.has_value());
    CHECK(read_res.error() == std::errc::bad_file_descriptor);

    auto write_res = FileSystem::Write(invalid_handle, std::span<const std::byte>(buf.data(), buf.size()), 0);
    CHECK(!write_res.has_value());
    CHECK(write_res.error() == std::errc::bad_file_descriptor);

    FileSystem::Close(invalid_handle);

    std::filesystem::path current_dir = ".";
    CHECK(FileSystem::Exists(current_dir));
    CHECK(FileSystem::IsDirectory(current_dir));

    std::filesystem::path resolved = FileSystem::Resolve("not_mounted/file.txt");
    CHECK(resolved.generic_string() == "not_mounted/file.txt");

    auto open_write_res = FileSystem::Open("empty_test.dat", FileAccess::Write);
    REQUIRE(open_write_res.has_value());
    FileHandle valid_handle = *open_write_res;
    
    auto empty_write_res = FileSystem::Write(valid_handle, std::span<const std::byte>(), 0);
    CHECK(empty_write_res.has_value());
    CHECK(*empty_write_res == 0);

    auto empty_read_res = FileSystem::Read(valid_handle, std::span<std::byte>(), 0);
    CHECK(empty_read_res.has_value());
    CHECK(*empty_read_res == 0);

    FileSystem::Close(valid_handle);
    FileSystem::Delete("empty_test.dat");
}
