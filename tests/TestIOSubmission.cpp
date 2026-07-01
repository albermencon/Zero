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

    // Wait for worker to process it
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

    PlatformCloseFile(handle);
    scheduler.Shutdown();
    std::filesystem::remove(tempFilePath);
}
