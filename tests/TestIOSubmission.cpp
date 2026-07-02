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

    Zero::InitJobSystem(1);

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

        scheduler.Release(ioHandle);
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

        scheduler.Release(handles[0]);
        scheduler.Release(handles[1]);
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

        scheduler.Release(h);
    }

    SUBCASE("Stream Scheduler")
    {
        std::vector<std::byte> readBuffer(testData.size());
        
        static std::atomic<uint32_t> chunksCompleted{ 0 };
        static std::atomic<bool> streamFinished{ false };
        
        chunksCompleted.store(0);
        streamFinished.store(false);

        Job chunkJob;
        chunkJob.mode = Job::Mode::Inline;
        chunkJob.fn = [](void* ctx) 
        {
            StreamChunkResult* chunkResult = static_cast<StreamChunkResult*>(ctx);
            // Just tracking completions for the test
            chunksCompleted.fetch_add(1);
        };

        Job streamJob;
        streamJob.mode = Job::Mode::Inline;
        streamJob.fn = [](void* ctx)
        {
            streamFinished.store(true);
        };

        StreamReadDescriptor streamDesc;
        streamDesc.file = handle;
        streamDesc.destination = { readBuffer.data(), readBuffer.size() };
        streamDesc.offset = 0;
        streamDesc.totalSize = readBuffer.size();
        streamDesc.chunkSize = 10; // very small chunk to force sliding window
        streamDesc.slidingWindowSize = 2; // 2 concurrent chunks
        streamDesc.chunkCompletionJob = chunkJob;
        streamDesc.streamCompletionJob = streamJob;

        StreamHandle streamHandle = scheduler.SubmitStream(streamDesc);
        REQUIRE(streamHandle.IsValid());

        // Wait for it to finish natively through internal progress tracker
        auto startTime = std::chrono::steady_clock::now();
        while (true)
        {
            IOProgress progress = scheduler.GetStreamProgress(streamHandle);
            if (progress.status == Status::Completed || progress.status == Status::Failed)
            {
                break;
            }
            if (std::chrono::steady_clock::now() - startTime > std::chrono::seconds(5))
            {
                FAIL("Stream timeout");
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        CHECK(scheduler.GetStreamProgress(streamHandle).status == Status::Completed);
        
        uint32_t totalExpectedChunks = static_cast<uint32_t>((readBuffer.size() + streamDesc.chunkSize - 1) / streamDesc.chunkSize);
        CHECK(chunksCompleted.load() == totalExpectedChunks);

        std::string readStr(reinterpret_cast<const char*>(readBuffer.data()), readBuffer.size());
        CHECK(readStr == testData);

        scheduler.ReleaseStream(streamHandle);
    }

    SUBCASE("Direct I/O Alignment Validation")
    {
        // Must align to physical sector sizes for O_DIRECT / FILE_FLAG_NO_BUFFERING
        alignas(4096) std::byte alignedBuffer[4096];
        
        std::string directFilePath = (std::filesystem::temp_directory_path() / "zero_io_direct_test.bin").string();

        // Ensure our temp file is exactly 4096 bytes for this test
        std::string directTestData(4096, 'D');
        {
            std::ofstream out(directFilePath, std::ios::binary);
            out.write(directTestData.data(), directTestData.size());
        }

        auto directHandleRes = PlatformOpenFile(directFilePath, FileAccess::Read, FileShare::Read, IOFlags::Direct);
        if (!directHandleRes)
        {
            MESSAGE("Direct IO Open Failed: ", directHandleRes.error().value(), " message: ", directHandleRes.error().message());
        }
        REQUIRE(directHandleRes.has_value());
        FileHandle directHandle = directHandleRes.value();

        JobCounter& counter = Zero::MakeCounter();
        counter.pending.store(1, std::memory_order_release);

        Job finishJob;
        finishJob.mode = Job::Mode::Inline;
        finishJob.fn = [](void*) {};

        ReadRequest directReq;
        directReq.file = directHandle;
        directReq.destination = { alignedBuffer, sizeof(alignedBuffer) };
        directReq.offset = 0;
        directReq.completionJob = finishJob;
        directReq.fence.counter = &counter;
        directReq.flags = IOFlags::Direct;

        IOHandle handle = scheduler.Submit(directReq);
        REQUIRE(handle.IsValid());

        counter.Wait();
        counter.Release();

        CHECK(scheduler.GetProgress(handle).status == Status::Completed);

        std::string readStr(reinterpret_cast<const char*>(alignedBuffer), sizeof(alignedBuffer));
        CHECK(readStr == directTestData);

        scheduler.Release(handle);

        PlatformCloseFile(directHandle);
        std::filesystem::remove(directFilePath);
    }

    SUBCASE("Slot Exhaustion and Explicit Release")
    {
        std::vector<std::byte> readBuffer(testData.size());
        std::vector<IOHandle> handles;
        handles.reserve(100);

        ReadRequest req;
        req.file = handle;
        req.destination = { readBuffer.data(), readBuffer.size() };
        req.offset = 0;
        req.completionJob.fn = nullptr;

        // Submit 100 requests to saturate the scheduler
        for (int i = 0; i < 100; ++i)
        {
            IOHandle h = scheduler.Submit(req);
            REQUIRE(h.IsValid());
            handles.push_back(h);
        }

        // 101st request should fail to allocate a slot
        IOHandle hFail = scheduler.Submit(req);
        CHECK(!hFail.IsValid());

        // Wait for at least one to complete so it can be released
        while (true)
        {
            if (scheduler.GetProgress(handles[0]).status == Status::Completed)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Release the first slot
        scheduler.Release(handles[0]);

        // 101st request should now succeed
        IOHandle hSucceed = scheduler.Submit(req);
        CHECK(hSucceed.IsValid());

        // Cleanup remaining handles (handles[0] was already released)
        for (size_t i = 1; i < handles.size(); ++i)
        {
            while (scheduler.GetProgress(handles[i]).status == Status::Pending || 
                   scheduler.GetProgress(handles[i]).status == Status::Executing)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            scheduler.Release(handles[i]);
        }
        while (scheduler.GetProgress(hSucceed).status == Status::Pending || 
               scheduler.GetProgress(hSucceed).status == Status::Executing)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        scheduler.Release(hSucceed);
    }

    PlatformCloseFile(handle);
    scheduler.Shutdown();
    Zero::ShutdownJobSystem();
    std::filesystem::remove(tempFilePath);
}
