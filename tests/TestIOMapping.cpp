#include "doctest.h"
#include <Engine/IO/IOMapping.h>
#include <Engine/IO/Platform/IOPlatform.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Zero;
using namespace Zero::IO;

TEST_CASE("IOMapping: MemoryMappedFile Lifecycle") 
{
    std::filesystem::path tempFilePath = std::filesystem::current_path() / "test_io_mapping.bin";
    
    // Create a 1 MB file
    size_t fileSize = 1024 * 1024;
    std::vector<uint8_t> testData(fileSize);
    for (size_t i = 0; i < fileSize; ++i) 
    {
        testData[i] = static_cast<uint8_t>(i % 256);
    }
    
    {
        std::ofstream out(tempFilePath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(testData.data()), fileSize);
    }

    SUBCASE("Map entire file read-only") 
    {
        auto fileExp = PlatformOpenFile(tempFilePath, FileAccess::Read, FileShare::Read);
        REQUIRE(fileExp.has_value());
        FileHandle handle = fileExp.value();

        auto mapExp = MemoryMappedFile::Map(handle, 0, fileSize, false);
        REQUIRE(mapExp.has_value());
        MemoryMappedFile mappedFile = std::move(mapExp.value());

        CHECK(mappedFile.IsMapped());
        CHECK(mappedFile.GetSize() == fileSize);
        
        const uint8_t* mappedData = static_cast<const uint8_t*>(mappedFile.GetAddress());
        REQUIRE(mappedData != nullptr);
        
        bool match = true;
        for (size_t i = 0; i < fileSize; ++i) 
        {
            if (mappedData[i] != testData[i]) 
            {
                match = false;
                break;
            }
        }
        CHECK(match);

        PlatformCloseFile(handle);
    }

    SUBCASE("Map partial file offset") 
    {
        auto fileExp = PlatformOpenFile(tempFilePath, FileAccess::Read, FileShare::Read);
        REQUIRE(fileExp.has_value());
        FileHandle handle = fileExp.value();

        size_t offset = 12345;
        size_t mapSize = 4096;
        auto mapExp = MemoryMappedFile::Map(handle, offset, mapSize, false);
        REQUIRE(mapExp.has_value());
        MemoryMappedFile mappedFile = std::move(mapExp.value());

        CHECK(mappedFile.IsMapped());
        CHECK(mappedFile.GetSize() == mapSize);
        
        const uint8_t* mappedData = static_cast<const uint8_t*>(mappedFile.GetAddress());
        REQUIRE(mappedData != nullptr);
        
        bool match = true;
        for (size_t i = 0; i < mapSize; ++i) 
        {
            if (mappedData[i] != testData[offset + i]) 
            {
                match = false;
                break;
            }
        }
        CHECK(match);

        PlatformCloseFile(handle);
    }

    std::filesystem::remove(tempFilePath);
}
