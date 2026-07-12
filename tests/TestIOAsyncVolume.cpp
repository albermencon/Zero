#include "doctest.h"
#include <Engine/IO/IOScheduler.h>
#include <Engine/IO/Platform/IOPlatform.h>
#include <Engine/JobSystem/JobSystem.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>

using namespace Zero;
using namespace Zero::IO;

namespace
{
    static std::atomic<uint32_t> largeChunkCompletions{ 0 };
    static std::atomic<bool> largeStreamFinished{ false };
}

TEST_CASE("IOScheduler: Async Large Volume Non-Blocking")
{
    std::filesystem::path tempFilePath = std::filesystem::current_path() / "test_io_large_volume.bin";
    std::filesystem::path concurrentFilePath = std::filesystem::current_path() / "test_io_concurrent.bin";

    // Clean up if left over
    std::error_code ec;
    std::filesystem::remove(tempFilePath, ec);
    std::filesystem::remove(concurrentFilePath, ec);

    largeChunkCompletions.store(0);
    largeStreamFinished.store(false);

    Zero::InitJobSystem(4);

    SchedulerConfig config;
    config.maxConcurrentRequests = 256;
    config.maxConcurrentStreams = 16;
    config.workerMode = WorkerMode::Automatic;

    Scheduler scheduler(config);
    scheduler.Init();

    // 20 MB of data
    std::vector<std::byte> largeBuffer(20 * 1024 * 1024);
    for (size_t i = 0; i < largeBuffer.size(); ++i)
    {
        largeBuffer[i] = static_cast<std::byte>(i & 0xFF);
    }

    auto openWriteRes = PlatformOpenFile(tempFilePath, FileAccess::ReadWrite, FileShare::Read);
    REQUIRE(openWriteRes.has_value());
    FileHandle writeHandle = openWriteRes.value();

    // Write asynchronously, verify non-blocking
    auto startSubmit = std::chrono::high_resolution_clock::now();

    WriteRequest writeReq{};
    writeReq.file = writeHandle;
    writeReq.source = { largeBuffer.data(), largeBuffer.size() };
    writeReq.offset = 0;
    writeReq.priority = Priority::Normal;

    IOHandle writeIOHandle = scheduler.Submit(writeReq);
    auto endSubmit = std::chrono::high_resolution_clock::now();
    auto submitDuration = std::chrono::duration_cast<std::chrono::microseconds>(endSubmit - startSubmit).count();

    REQUIRE(writeIOHandle.IsValid());
    // Submit must return almost instantly (e.g. < 5000 microseconds)
    CHECK(submitDuration < 5000);

    // Wait for the write to finish
    while (true)
    {
        IOProgress progress = scheduler.GetProgress(writeIOHandle);
        if (progress.status == Status::Completed)
        {
            break;
        }
        else if (progress.status == Status::Failed || progress.status == Status::Cancelled)
        {
            FAIL("Large write failed");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    scheduler.Release(writeIOHandle);
    PlatformCloseFile(writeHandle);

    // Now, open for read
    auto openReadRes = PlatformOpenFile(tempFilePath, FileAccess::Read, FileShare::Read);
    REQUIRE(openReadRes.has_value());
    FileHandle readHandle = openReadRes.value();

    std::vector<std::byte> readBuffer(largeBuffer.size());
    std::memset(readBuffer.data(), 0, readBuffer.size());

    // 1 MB chunks, sliding window = 4
    size_t chunkSize = 1 * 1024 * 1024;
    uint32_t slidingWindowSize = 4;

    Job chunkJob;
    chunkJob.mode = Job::Mode::Inline;
    chunkJob.fn = [](void* ctx) {
        auto* result = static_cast<StreamChunkResult*>(ctx);
        largeChunkCompletions.fetch_add(1, std::memory_order_relaxed);
    };

    Job streamJob = make_job_external([](void* ptr) {
        auto* finishedPtr = static_cast<std::atomic<bool>*>(ptr);
        finishedPtr->store(true, std::memory_order_release);
    }, &largeStreamFinished);

    StreamReadDescriptor streamDesc{};
    streamDesc.file = readHandle;
    streamDesc.destination = { readBuffer.data(), readBuffer.size() };
    streamDesc.offset = 0;
    streamDesc.totalSize = readBuffer.size();
    streamDesc.chunkSize = chunkSize;
    streamDesc.slidingWindowSize = slidingWindowSize;
    streamDesc.chunkCompletionJob = chunkJob;
    streamDesc.streamCompletionJob = streamJob;

    StreamHandle streamHandle = scheduler.SubmitStream(streamDesc);
    REQUIRE(streamHandle.IsValid());

    // WHILE STREAMING IS ACTIVE, submit independent operations on a different file
    auto openRes2 = PlatformOpenFile(concurrentFilePath, FileAccess::ReadWrite, FileShare::Read);
    REQUIRE(openRes2.has_value());
    FileHandle handle2 = openRes2.value();

    std::string concurrentText = "Concurrent non-blocking read/write operations executing in parallel with large stream.";
    std::vector<std::byte> writeBuf2(concurrentText.size());
    std::memcpy(writeBuf2.data(), concurrentText.data(), concurrentText.size());

    WriteRequest writeReq2{};
    writeReq2.file = handle2;
    writeReq2.source = { writeBuf2.data(), writeBuf2.size() };
    writeReq2.offset = 0;
    writeReq2.priority = Priority::Critical;

    IOHandle ioHandle2 = scheduler.Submit(writeReq2);
    REQUIRE(ioHandle2.IsValid());

    // This independent critical write should complete very quickly
    while (true)
    {
        if (scheduler.GetProgress(ioHandle2).status == Status::Completed)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    scheduler.Release(ioHandle2);

    // Now read it back asynchronously
    std::vector<std::byte> readBuf2(concurrentText.size());
    ReadRequest readReq2{};
    readReq2.file = handle2;
    readReq2.destination = { readBuf2.data(), readBuf2.size() };
    readReq2.offset = 0;
    readReq2.priority = Priority::Critical;

    IOHandle ioHandle3 = scheduler.Submit(readReq2);
    REQUIRE(ioHandle3.IsValid());

    while (true)
    {
        if (scheduler.GetProgress(ioHandle3).status == Status::Completed)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::string readText2(reinterpret_cast<const char*>(readBuf2.data()), readBuf2.size());
    CHECK(readText2 == concurrentText);

    scheduler.Release(ioHandle3);
    PlatformCloseFile(handle2);
    std::filesystem::remove(concurrentFilePath);

    // Wait for the main stream to finish
    auto streamStartTime = std::chrono::steady_clock::now();
    while (!largeStreamFinished.load(std::memory_order_acquire))
    {
        IOProgress p = scheduler.GetStreamProgress(streamHandle);
        if (p.status == Status::Failed || p.status == Status::Cancelled)
        {
            FAIL("Stream failed");
            break;
        }
        if (std::chrono::steady_clock::now() - streamStartTime > std::chrono::seconds(15))
        {
            FAIL("Stream timed out");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    CHECK(scheduler.GetStreamProgress(streamHandle).status == Status::Completed);
    CHECK(largeChunkCompletions.load() == 20);

    // Verify stream buffer matches largeBuffer
    bool matches = true;
    for (size_t i = 0; i < readBuffer.size(); ++i)
    {
        if (readBuffer[i] != largeBuffer[i])
        {
            matches = false;
            break;
        }
    }
    CHECK(matches);

    scheduler.ReleaseStream(streamHandle);
    PlatformCloseFile(readHandle);
    std::filesystem::remove(tempFilePath);

    scheduler.Shutdown();
    Zero::ShutdownJobSystem();
}

TEST_CASE("IOScheduler: Batch Submission and Priority")
{
    std::filesystem::path tempFilePath = std::filesystem::current_path() / "test_io_priority.bin";
    std::error_code ec;
    std::filesystem::remove(tempFilePath, ec);

    Zero::InitJobSystem(2);

    SchedulerConfig config;
    config.maxConcurrentRequests = 64;
    config.workerMode = WorkerMode::Automatic;
    config.workerCount = 2;

    Scheduler scheduler(config);
    scheduler.Init();

    // Create a 1 MB file
    std::vector<std::byte> writeData(1024 * 1024);
    for (size_t i = 0; i < writeData.size(); ++i)
    {
        writeData[i] = static_cast<std::byte>(i & 0xFF);
    }
    
    // Setup file
    {
        std::ofstream out(tempFilePath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(writeData.data()), writeData.size());
    }

    auto openRes = PlatformOpenFile(tempFilePath, FileAccess::ReadWrite, FileShare::Read);
    REQUIRE(openRes.has_value());
    FileHandle fileHandle = openRes.value();

    // Submit 8 concurrent reads at different offsets and different priorities in a batch
    const int numReads = 8;
    std::vector<std::vector<std::byte>> readBuffers(numReads, std::vector<std::byte>(1024));
    std::vector<ReadRequest> readReqs(numReads, ReadRequest{});
    std::vector<IOHandle> readHandles(numReads);

    // Mix priorities: Critical, High, Normal, Low, Background
    Priority priorities[] = {
        Priority::Background,
        Priority::Low,
        Priority::Normal,
        Priority::High,
        Priority::Critical,
        Priority::Normal,
        Priority::High,
        Priority::Critical
    };

    for (int i = 0; i < numReads; ++i)
    {
        readReqs[i].file = fileHandle;
        readReqs[i].destination = { readBuffers[i].data(), readBuffers[i].size() };
        readReqs[i].offset = i * 1024 * 10; // spaced out offsets
        readReqs[i].priority = priorities[i];
        readReqs[i].completionJob.fn = nullptr;
    }

    // Submit using SubmitBatch
    scheduler.SubmitBatch(readReqs, readHandles);

    for (int i = 0; i < numReads; ++i)
    {
        REQUIRE(readHandles[i].IsValid());
    }

    // Wait for all to complete
    while (true)
    {
        bool allDone = true;
        for (int i = 0; i < numReads; ++i)
        {
            Status status = scheduler.GetProgress(readHandles[i]).status;
            if (status == Status::Pending || status == Status::Executing)
            {
                allDone = false;
                break;
            }
        }
        if (allDone) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Verify all read buffers match expected slices of writeData
    for (int i = 0; i < numReads; ++i)
    {
        CHECK(scheduler.GetProgress(readHandles[i]).status == Status::Completed);
        size_t expectedOffset = i * 1024 * 10;
        bool match = true;
        for (size_t j = 0; j < readBuffers[i].size(); ++j)
        {
            if (readBuffers[i][j] != writeData[expectedOffset + j])
            {
                match = false;
                break;
            }
        }
        CHECK(match);
        scheduler.Release(readHandles[i]);
    }

    PlatformCloseFile(fileHandle);
    std::filesystem::remove(tempFilePath);
    scheduler.Shutdown();
    Zero::ShutdownJobSystem();
}

TEST_CASE("IOScheduler: Request and Stream Cancellation")
{
    std::filesystem::path tempFilePath = std::filesystem::current_path() / "test_io_cancel.bin";
    std::error_code ec;
    std::filesystem::remove(tempFilePath, ec);

    Zero::InitJobSystem(2);

    SchedulerConfig config;
    config.maxConcurrentRequests = 100;
    config.workerMode = WorkerMode::Automatic;
    config.workerCount = 1;

    Scheduler scheduler(config);
    scheduler.Init();

    // Create a 2 MB file
    std::vector<std::byte> writeData(2 * 1024 * 1024);
    std::memset(writeData.data(), 'A', writeData.size());
    {
        std::ofstream out(tempFilePath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(writeData.data()), writeData.size());
    }

    auto openRes = PlatformOpenFile(tempFilePath, FileAccess::Read, FileShare::Read);
    REQUIRE(openRes.has_value());
    FileHandle fileHandle = openRes.value();

    // Submit many requests to saturate the queue
    const int numRequests = 40;
    std::vector<std::vector<std::byte>> readBuffers(numRequests, std::vector<std::byte>(1024 * 10)); // 10 KB each
    std::vector<ReadRequest> readReqs(numRequests, ReadRequest{});
    std::vector<IOHandle> readHandles(numRequests);

    for (int i = 0; i < numRequests; ++i)
    {
        readReqs[i].file = fileHandle;
        readReqs[i].destination = { readBuffers[i].data(), readBuffers[i].size() };
        readReqs[i].offset = 0;
        readReqs[i].priority = Priority::Background;
        readReqs[i].completionJob.fn = nullptr;
    }

    // Submit all
    scheduler.SubmitBatch(readReqs, readHandles);

    // Cancel the last request immediately
    IOHandle targetHandle = readHandles[numRequests - 1];
    CancelResult cancelRes = scheduler.Cancel(targetHandle);
    
    CHECK((cancelRes == CancelResult::Cancelled || cancelRes == CancelResult::AlreadyCompleted || cancelRes == CancelResult::NotCancelable));

    if (cancelRes == CancelResult::Cancelled)
    {
        CHECK(scheduler.GetProgress(targetHandle).status == Status::Cancelled);
    }

    // Wait for all requests to finish/clear
    for (int i = 0; i < numRequests; ++i)
    {
        while (true)
        {
            Status status = scheduler.GetProgress(readHandles[i]).status;
            if (status == Status::Completed || status == Status::Failed || status == Status::Cancelled)
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        scheduler.Release(readHandles[i]);
    }

    // Now test Stream Cancellation
    std::vector<std::byte> streamBuf(writeData.size());
    StreamReadDescriptor streamDesc{};
    streamDesc.file = fileHandle;
    streamDesc.destination = { streamBuf.data(), streamBuf.size() };
    streamDesc.offset = 0;
    streamDesc.totalSize = streamBuf.size();
    streamDesc.chunkSize = 16 * 1024;
    streamDesc.slidingWindowSize = 4;
    streamDesc.chunkCompletionJob.fn = nullptr;
    streamDesc.streamCompletionJob.fn = nullptr;

    StreamHandle streamHandle = scheduler.SubmitStream(streamDesc);
    REQUIRE(streamHandle.IsValid());

    // Cancel the stream immediately
    CancelResult streamCancelRes = scheduler.CancelStream(streamHandle);
    CHECK((streamCancelRes == CancelResult::Cancelled || streamCancelRes == CancelResult::AlreadyCompleted));

    // Wait a bit to ensure the cancel takes effect
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    IOProgress streamProgress = scheduler.GetStreamProgress(streamHandle);
    CHECK((streamProgress.status == Status::Cancelled || streamProgress.status == Status::Completed));

    scheduler.ReleaseStream(streamHandle);
    PlatformCloseFile(fileHandle);
    std::filesystem::remove(tempFilePath);
    scheduler.Shutdown();
    Zero::ShutdownJobSystem();
}
