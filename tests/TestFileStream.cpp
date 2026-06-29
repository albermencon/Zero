#include <doctest.h>
#include <Engine/File/FileStream.h>
#include <Engine/File/FileSystem.h>
#include <filesystem>
#include <vector>
#include <string>
#include <system_error>

using namespace Zero;

TEST_CASE("FileStream: Full API Coverage")
{
    std::filesystem::path test_path = "filestream_coverage.dat";
    if (FileSystem::Exists(test_path))
    {
        FileSystem::Delete(test_path);
    }

    SUBCASE("Open non-existent file for read fails")
    {
        auto stream_res = FileStream::Open("does_not_exist_xyz.dat", FileAccess::Read);
        CHECK(!stream_res.has_value());
    }

    SUBCASE("Write Operations and Seek")
    {
        auto stream_res = FileStream::Open(test_path, FileAccess::Write);
        REQUIRE(stream_res.has_value());
        FileStream& stream = *stream_res;

        CHECK(stream.IsOpen());
        CHECK(stream.GetPosition() == 0);
        CHECK(stream.GetSize() == 0);

        int val = 12345;
        auto write_val_res = stream.WriteValue(val);
        CHECK(write_val_res.has_value());
        CHECK(stream.GetPosition() == sizeof(int));
        CHECK(stream.GetSize() == sizeof(int));

        std::string text = "Stream Coverage";
        auto write_str_res = stream.WriteString(text);
        CHECK(write_str_res.has_value());
        
        size_t expected_size = sizeof(int) + sizeof(uint32_t) + text.size();
        CHECK(stream.GetPosition() == expected_size);
        CHECK(stream.GetSize() == expected_size);

        auto write_empty_res = stream.WriteString("");
        CHECK(write_empty_res.has_value());
        expected_size += sizeof(uint32_t);
        CHECK(stream.GetPosition() == expected_size);

        std::vector<std::byte> raw_data = { std::byte{0xAA}, std::byte{0xBB} };
        auto write_raw = stream.Write(raw_data);
        CHECK(write_raw.has_value());
        CHECK(*write_raw == 2);
        expected_size += 2;
        CHECK(stream.GetPosition() == expected_size);
        CHECK(stream.GetSize() == expected_size);

        stream.Close();
        CHECK(!stream.IsOpen());
    }

    SUBCASE("Read Operations and Seek")
    {
        {
            auto write_stream = FileStream::Open(test_path, FileAccess::Write).value();
            (void)write_stream.WriteValue(12345);
            (void)write_stream.WriteString("Stream Coverage");
            (void)write_stream.WriteString("");
            std::vector<std::byte> raw_data = { std::byte{0xAA}, std::byte{0xBB} };
            (void)write_stream.Write(raw_data);
        }

        auto stream_res = FileStream::Open(test_path, FileAccess::Read);
        REQUIRE(stream_res.has_value());
        FileStream& stream = *stream_res;

        CHECK(stream.IsOpen());
        CHECK(stream.GetPosition() == 0);
        CHECK(stream.GetSize() > 0);

        int val = 0;
        auto read_val_res = stream.ReadValue(val);
        CHECK(read_val_res.has_value());
        CHECK(val == 12345);

        auto read_str_res = stream.ReadString();
        REQUIRE(read_str_res.has_value());
        CHECK(*read_str_res == "Stream Coverage");

        auto read_empty_str = stream.ReadString();
        REQUIRE(read_empty_str.has_value());
        CHECK(*read_empty_str == "");

        std::vector<std::byte> raw_read(2);
        auto read_raw = stream.Read(raw_read);
        CHECK(read_raw.has_value());
        CHECK(*read_raw == 2);
        CHECK(raw_read[0] == std::byte{0xAA});
        CHECK(raw_read[1] == std::byte{0xBB});

        stream.Seek(0);
        CHECK(stream.GetPosition() == 0);
        int val2 = 0;
        auto read_val_res2 = stream.ReadValue(val2);
        CHECK(read_val_res2.has_value());
        CHECK(val2 == 12345);

        stream.Seek(stream.GetSize());
        std::vector<std::byte> overflow_buf(10);
        auto eof_read = stream.Read(overflow_buf);
        CHECK(eof_read.has_value());
        CHECK(*eof_read == 0); 
    }

    SUBCASE("Move Semantics")
    {
        {
            auto write_stream = FileStream::Open(test_path, FileAccess::Write).value();
        }

        auto stream_res = FileStream::Open(test_path, FileAccess::Read);
        REQUIRE(stream_res.has_value());
        FileStream stream1 = std::move(*stream_res);
        
        CHECK(stream1.IsOpen());

        FileStream stream2;
        CHECK(!stream2.IsOpen());

        stream2 = std::move(stream1);
        CHECK(!stream1.IsOpen());
        CHECK(stream2.IsOpen());
    }

    FileSystem::Delete(test_path);
}
