#include <doctest.h>
#include <Engine/IO/Platform/IOPlatform.h>
#include <Engine/File/FileSystem.h>
#include <filesystem>
#include <fstream>
#include <system_error>

using namespace Zero;
using namespace Zero::IO;

TEST_CASE("IOPlatform: OpenAndCloseFile_Read") {
    std::filesystem::path tempFilePath = std::filesystem::temp_directory_path() / "zero_io_test_file_read.bin";
    
    {
        std::ofstream ofs(tempFilePath, std::ios::binary);
        ofs.write("HelloZeroIO", 11);
    }

    auto handleExpected = PlatformOpenFile(tempFilePath, FileAccess::Read, FileShare::Read);
    REQUIRE(handleExpected.has_value());

    FileHandle handle = handleExpected.value();
    CHECK(static_cast<bool>(handle));
    CHECK(handle.size == 11);

    PlatformCloseFile(handle);
    CHECK(!static_cast<bool>(handle));

    std::error_code ec;
    std::filesystem::remove(tempFilePath, ec);
}

TEST_CASE("IOPlatform: OpenNonExistentFile") {
    std::filesystem::path tempFilePath = std::filesystem::temp_directory_path() / "zero_io_non_existent_file.bin";
    
    std::error_code ec;
    std::filesystem::remove(tempFilePath, ec);

    auto handleExpected = PlatformOpenFile(tempFilePath, FileAccess::Read, FileShare::Read);
    CHECK(!handleExpected.has_value());
}

TEST_CASE("IOPlatform: OpenFile_WriteAlways") {
    std::filesystem::path tempFilePath = std::filesystem::temp_directory_path() / "zero_io_test_write.bin";
    
    std::error_code ec;
    std::filesystem::remove(tempFilePath, ec);

    auto handleExpected = PlatformOpenFile(tempFilePath, FileAccess::Write, FileShare::None);
    REQUIRE(handleExpected.has_value());

    FileHandle handle = handleExpected.value();
    CHECK(static_cast<bool>(handle));
    CHECK(handle.size == 0);

    PlatformCloseFile(handle);
    CHECK(!static_cast<bool>(handle));

    std::filesystem::remove(tempFilePath, ec);
}

TEST_CASE("IOPlatform: OpenFile_DirectFlag") {
    std::filesystem::path tempFilePath = std::filesystem::temp_directory_path() / "zero_io_test_direct.bin";
    
    {
        std::ofstream ofs(tempFilePath, std::ios::binary);
        ofs.write("TestingDirectIO", 15);
    }

    auto handleExpected = PlatformOpenFile(tempFilePath, FileAccess::Read, FileShare::Read, IOFlags::Direct);
    
    if (handleExpected.has_value()) {
        FileHandle handle = handleExpected.value();
        PlatformCloseFile(handle);
        CHECK(!static_cast<bool>(handle));
    } else {
        // Failing might be normal if the FS or temp directory doesn't support O_DIRECT/FILE_FLAG_NO_BUFFERING
        // We ensure we got an OS error code at least.
        CHECK(handleExpected.error().value() != 0);
    }

    std::error_code ec;
    std::filesystem::remove(tempFilePath, ec);
}
