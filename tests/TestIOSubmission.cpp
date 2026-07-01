#include "doctest.h"
#include <Engine/IO/IOScheduler.h>
#include <Engine/IO/Platform/IOPlatform.h>
#include <Engine/JobSystem/JobSystem.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>

using namespace Zero;
using namespace Zero::IO;

TEST_CASE("IOScheduler: End-to-End Submission Fallback") 
{
    std::filesystem::path tempFilePath = std::filesystem::current_path() / "test_io_submission.txt";
    std::string testData = "Zero Engine IO Test Data string - End to End test.";
    
    {
        std::ofstream out(tempFilePath);
        out << testData;
    }

    SchedulerConfig config;
    config.maxConcurrentRequests = 100;
    config.workerCount = 2;
    config.workerMode = WorkerMode::Manual;

    Scheduler scheduler(config);
    scheduler.Init();

    auto fileExp = PlatformOpenFile(tempFilePath, FileAccess::ReadWrite, FileShare::Read);
    REQUIRE(fileExp.has_value());
    FileHandle handle = fileExp.value();

    SUBCASE("Single Read Request")
    {
        std::vector<std::byte> readBuffer(testData.size());
        
        Job completionJob;
        completionJob.fn = nullptr;
        completionJob.ptr = nullptr;
        completionJob.mode = Job::Mode::External;

        ReadRequest readReq;
        readReq.file = handle;
        readReq.destination = { readBuffer.data(), readBuffer.size() };
        readReq.offset = 0;
        readReq.completionJob = completionJob;
        
        IOHandle ioHandle = scheduler.Submit(readReq);
        REQUIRE(ioHandle.IsValid());

        while (true)
        {
            IOProgress progress = scheduler.GetProgress(ioHandle);
            if (progress.status == Status::Completed || progress.status == Status::Failed || progress.status == Status::Cancelled)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        IOProgress progress = scheduler.GetProgress(ioHandle);
        CHECK(progress.status == Status::Completed);
        CHECK(progress.bytesTransferred == testData.size());

        std::string readStr(reinterpret_cast<const char*>(readBuffer.data()), readBuffer.size());
        CHECK(readStr == testData);
    }

    SUBCASE("Batch Submit")
    {
        std::vector<std::byte> buf1(4);
        std::vector<std::byte> buf2(6);

        ReadRequest r1;
        r1.file = handle;
        r1.destination = { buf1.data(), buf1.size() };
        r1.offset = 0;
        
        ReadRequest r2;
        r2.file = handle;
        r2.destination = { buf2.data(), buf2.size() };
        r2.offset = 5; // "Zero E" skips " ", starts at 5: "Engine"

        ReadRequest requests[] = { r1, r2 };
        IOHandle handles[2];

        scheduler.SubmitBatch(requests, handles);

        REQUIRE(handles[0].IsValid());
        REQUIRE(handles[1].IsValid());

        while (true)
        {
            auto p1 = scheduler.GetProgress(handles[0]);
            auto p2 = scheduler.GetProgress(handles[1]);
            if ((p1.status == Status::Completed || p1.status == Status::Failed) &&
                (p2.status == Status::Completed || p2.status == Status::Failed))
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        CHECK(scheduler.GetProgress(handles[0]).status == Status::Completed);
        CHECK(scheduler.GetProgress(handles[1]).status == Status::Completed);

        std::string str1(reinterpret_cast<const char*>(buf1.data()), buf1.size());
        std::string str2(reinterpret_cast<const char*>(buf2.data()), buf2.size());

        CHECK(str1 == "Zero");
        CHECK(str2 == "Engine");
    }

    SUBCASE("Scatter Read")
    {
        std::vector<std::byte> buf1(4);
        std::vector<std::byte> buf2(6);

        ScatterRange ranges[] = {
            { 0, { buf1.data(), buf1.size() } },
            { 5, { buf2.data(), buf2.size() } }
        };

        ReadScatterRequest req;
        req.file = handle;
        req.ranges = ranges;
        
        IOHandle h = scheduler.SubmitScatter(req);
        REQUIRE(h.IsValid());

        while (true)
        {
            auto p = scheduler.GetProgress(h);
            if (p.status == Status::Completed || p.status == Status::Failed)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        CHECK(scheduler.GetProgress(h).status == Status::Completed);
        CHECK(scheduler.GetProgress(h).bytesTransferred == 10);

        std::string str1(reinterpret_cast<const char*>(buf1.data()), buf1.size());
        std::string str2(reinterpret_cast<const char*>(buf2.data()), buf2.size());

        CHECK(str1 == "Zero");
        CHECK(str2 == "Engine");
    }

    PlatformCloseFile(handle);
    scheduler.Shutdown();
    std::filesystem::remove(tempFilePath);
}
