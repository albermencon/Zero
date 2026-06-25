#include "Engine/Profiler/Profiler.h"
#include "pch.h"
#include "uv.h"
#include <Engine/File/FileWatcher.h>
#include <Engine/Thread/Thread.h>
#include <Engine/Core.h>
#include <Engine/Log.h>
#include <unordered_map>
#include <Engine/Time.h>
#include <vector>
#include <mutex>
#include <string>
#include <chrono>
#include <atomic>
#include <concurrentqueue.h>

namespace Zero
{
    struct FileWatcher::Impl
    {
        struct WatchInfo
        {
            std::filesystem::path watchPath;
            uv_fs_event_t handle;
            int refCount = 0;
        };

        enum class CommandType
        {
            Watch,
            UnWatch,
            UnWatchAll,
            Shutdown
        };

        struct Command
        {
            CommandType type;
            std::filesystem::path path;
            WatchHandle handle;
        };

        struct CallbackEntry
        {
            FileCallback callback;
            void* userData;
            bool isDirectory;
            std::filesystem::path path; // Normalized path
        };

        struct DispatchEntry
        {
            FileCallback callback;
            void* userData;
            FileEvent event;
        };

        moodycamel::ConcurrentQueue<FileEvent> m_eventBuffer;
        
        uv_loop_t m_loop;
        uv_async_t m_wakeup;
        Zero::Thread m_thread;
        std::atomic<bool> m_running{false};

        std::mutex m_commandMutex;
        std::vector<Command> m_pendingCommands;

        std::unordered_map<std::string, std::unique_ptr<WatchInfo>> m_activeWatches;

        std::mutex m_registryMutex;
        WatchHandle m_nextHandle = 1;
        std::unordered_map<WatchHandle, CallbackEntry> m_callbacks;

        std::unordered_map<std::string, FileEvent> m_coalescedEvents;
        std::vector<DispatchEntry> m_dispatches;
    };

    namespace
    {
        // Simple subpath check on pre-normalized paths.
        // E.g., check if "src/a/b.cpp" is under "src/a"
        bool IsSubpathNormalized(const std::filesystem::path& path, const std::filesystem::path& base)
        {
            auto pathIt = path.begin();
            auto baseIt = base.begin();
            while (baseIt != base.end() && pathIt != path.end())
            {
                if (*pathIt != *baseIt)
                    return false;
                ++pathIt;
                ++baseIt;
            }
            return baseIt == base.end();
        }
    }

    FileWatcher& FileWatcher::Get()
    {
        static FileWatcher instance;
        return instance;
    }

    FileWatcher::FileWatcher()
    {
        m_Impl = std::make_unique<Impl>();
    }

    void FileWatcher::Init()
    {
        m_Impl->m_running.store(true, std::memory_order_release);

        int rc = uv_loop_init(&m_Impl->m_loop);
        ZERO_CORE_ASSERT(rc == 0, "FileWatcher::Init: Failed to initialize UV loop");

        m_Impl->m_wakeup.data = this;
        rc = uv_async_init(&m_Impl->m_loop, &m_Impl->m_wakeup, [](uv_async_t* handle) {
            auto* self = static_cast<FileWatcher*>(handle->data);
            self->ProcessPendingCommands();
        });
        ZERO_CORE_ASSERT(rc == 0, "FileWatcher::Init: Failed to initialize UV async");

        m_Impl->m_thread = Zero::Thread(&FileWatcher::WatchLoop, this);
    }

    void FileWatcher::Shutdown()
    {
        if (!m_Impl->m_running.load(std::memory_order_acquire))
            return;

        m_Impl->m_running.store(false, std::memory_order_release);

        {
            std::lock_guard<std::mutex> lock(m_Impl->m_commandMutex);
            m_Impl->m_pendingCommands.push_back({ Impl::CommandType::Shutdown, {}, InvalidWatchHandle });
        }
        uv_async_send(&m_Impl->m_wakeup);

        m_Impl->m_thread.Join();
        
        int rc = uv_loop_close(&m_Impl->m_loop);
        ZERO_CORE_ASSERT(rc == 0, "FileWatcher::Shutdown: Failed to close UV loop cleanly. Memory leak!");

        m_Impl.reset();
    }

    FileWatcher::~FileWatcher()
    {
        if (m_Impl) Shutdown();
    }

    WatchHandle FileWatcher::WatchDirectory(const std::filesystem::path& path, FileCallback callback, void* userData)
    {
        ZERO_PROFILE_FUNCTION();
        std::error_code ec;
        std::filesystem::path normPath = std::filesystem::canonical(path, ec);
        if (ec) return InvalidWatchHandle;

        WatchHandle handle = 0;

        {
            std::lock_guard<std::mutex> lock(m_Impl->m_registryMutex);
            handle = m_Impl->m_nextHandle++;
            m_Impl->m_callbacks[handle] = { callback, userData, true, normPath };
        }

        {
            std::lock_guard<std::mutex> lock(m_Impl->m_commandMutex);
            m_Impl->m_pendingCommands.push_back({ Impl::CommandType::Watch, normPath, handle });
        }

        uv_async_send(&m_Impl->m_wakeup);
        return handle;
    }

    WatchHandle FileWatcher::WatchFile(const std::filesystem::path& path, FileCallback callback, void* userData)
    {
        ZERO_PROFILE_FUNCTION();
        std::error_code ec;
        std::filesystem::path normPath = std::filesystem::canonical(path, ec);
        if (ec) return InvalidWatchHandle;

        WatchHandle handle = 0;

        {
            std::lock_guard<std::mutex> lock(m_Impl->m_registryMutex);
            handle = m_Impl->m_nextHandle++;
            m_Impl->m_callbacks[handle] = { callback, userData, false, normPath };
        }

        {
            std::lock_guard<std::mutex> lock(m_Impl->m_commandMutex);
            m_Impl->m_pendingCommands.push_back({ Impl::CommandType::Watch, normPath, handle });
        }

        uv_async_send(&m_Impl->m_wakeup);
        return handle;
    }

    void FileWatcher::UnWatch(WatchHandle handle)
    {
        ZERO_PROFILE_FUNCTION();
        if (handle == InvalidWatchHandle) return;

        std::filesystem::path watchedPath;
        {
            std::lock_guard<std::mutex> lock(m_Impl->m_registryMutex);
            auto it = m_Impl->m_callbacks.find(handle);
            if (it == m_Impl->m_callbacks.end()) return;

            watchedPath = it->second.path;
            
            // Explicitly erase callback so it is never invoked again
            m_Impl->m_callbacks.erase(it);
        }

        {
            std::lock_guard<std::mutex> lock(m_Impl->m_commandMutex);
            m_Impl->m_pendingCommands.push_back({ Impl::CommandType::UnWatch, watchedPath, handle });
        }

        uv_async_send(&m_Impl->m_wakeup);
    }

    void FileWatcher::UnWatchAll()
    {
        ZERO_PROFILE_FUNCTION();

        {
            ZERO_PROFILE_SCOPE("Mutex registry");
            std::lock_guard<std::mutex> lock(m_Impl->m_registryMutex);
            m_Impl->m_callbacks.clear();
        }

        {
            ZERO_PROFILE_SCOPE("Mutex command");
            std::lock_guard<std::mutex> lock(m_Impl->m_commandMutex);
            m_Impl->m_pendingCommands.push_back({ Impl::CommandType::UnWatchAll, {}, InvalidWatchHandle });
        }

        uv_async_send(&m_Impl->m_wakeup);
    }

    void FileWatcher::Poll()
    {
        ZERO_PROFILE_FUNCTION()

        m_Impl->m_coalescedEvents.clear();
        m_Impl->m_dispatches.clear();

        FileEvent ev;

        while (m_Impl->m_eventBuffer.try_dequeue(ev))
        {
            if (ev.type == FileEventType::Renamed)
            {
                std::error_code ec;
                bool exists = std::filesystem::exists(ev.path, ec);
                ev.type = exists ? FileEventType::Created : FileEventType::Deleted;
            }

            std::string pathStr = ev.path.generic_string();
            auto it = m_Impl->m_coalescedEvents.find(pathStr);
            if (it == m_Impl->m_coalescedEvents.end())
            {
                m_Impl->m_coalescedEvents[pathStr] = std::move(ev);
            }
            else
            {
                auto& existing = it->second;
                if (existing.type == FileEventType::Created && ev.type == FileEventType::Deleted)
                {
                    m_Impl->m_coalescedEvents.erase(it);
                }
                else if (existing.type == FileEventType::Created && ev.type == FileEventType::Modified)
                {
                    existing.timeSinceEpoch = ev.timeSinceEpoch;
                }
                else if (existing.type == FileEventType::Modified && ev.type == FileEventType::Modified)
                {
                    existing.timeSinceEpoch = ev.timeSinceEpoch;
                }
                else
                {
                    m_Impl->m_coalescedEvents[pathStr] = std::move(ev);
                }
            }
        }

        if (m_Impl->m_coalescedEvents.empty()) return;

        {
            ZERO_PROFILE_SCOPE("Mutex registry")
            std::lock_guard<std::mutex> lock(m_Impl->m_registryMutex);
            for (const auto& [pathStr, netEvent] : m_Impl->m_coalescedEvents)
            {
                for (const auto& [handle, cbEntry] : m_Impl->m_callbacks)
                {
                    if (cbEntry.isDirectory)
                    {
                        if (IsSubpathNormalized(netEvent.path, cbEntry.path))
                            m_Impl->m_dispatches.push_back({ cbEntry.callback, cbEntry.userData, netEvent });
                    }
                    else
                    {
                        if (netEvent.path == cbEntry.path)
                            m_Impl->m_dispatches.push_back({ cbEntry.callback, cbEntry.userData, netEvent });
                    }
                }
            }
        }

        for (const auto& dispatch : m_Impl->m_dispatches)
        {
            if (dispatch.callback)
                dispatch.callback(dispatch.event, dispatch.userData);
        }
    }

    void FileWatcher::WatchLoop()
    {
        ZERO_PROFILE_THREAD("FileWatcher");
        ZERO_PROFILE_FUNCTION();
        int rc = uv_run(&m_Impl->m_loop, UV_RUN_DEFAULT);
        ZERO_CORE_ASSERT(rc == 0, "FileWatcher::WatchLoop: Failed to run UV loop");
    }

    void FileWatcher::ProcessPendingCommands()
    {
        ZERO_PROFILE_FUNCTION();
        std::vector<Impl::Command> commands;
        {
            std::lock_guard<std::mutex> lock(m_Impl->m_commandMutex);
            commands.swap(m_Impl->m_pendingCommands);
        }

        for (const auto& cmd : commands)
        {
            if (cmd.type == Impl::CommandType::Shutdown)
            {
                for (auto& [p, info] : m_Impl->m_activeWatches)
                {
                    auto* rawInfo = info.release();
                    uv_close(reinterpret_cast<uv_handle_t*>(&rawInfo->handle), [](uv_handle_t* h) {
                        delete static_cast<Impl::WatchInfo*>(h->data);
                    });
                }
                m_Impl->m_activeWatches.clear();

                uv_close(reinterpret_cast<uv_handle_t*>(&m_Impl->m_wakeup), nullptr);
                return;
            }

            std::string pathStr = cmd.path.generic_string();

            if (cmd.type == Impl::CommandType::Watch)
            {
                auto it = m_Impl->m_activeWatches.find(pathStr);
                if (it != m_Impl->m_activeWatches.end())
                {
                    it->second->refCount++;
                    continue;
                }

                auto* info = new Impl::WatchInfo();
                info->watchPath = cmd.path;
                info->refCount = 1;

                int rc = uv_fs_event_init(&m_Impl->m_loop, &info->handle);
                if (rc != 0)
                {
                    delete info;
                    continue;
                }

                info->handle.data = info;

                rc = uv_fs_event_start(&info->handle, [](uv_fs_event_t* handle, const char* filename, int events, int status) {
                    ZERO_PROFILE_FUNCTION();
                    if (status < 0) return;

                    auto* info = static_cast<Impl::WatchInfo*>(handle->data);
                    if (!info) return;

                    std::filesystem::path fullPath = info->watchPath;
                    if (filename)
                        fullPath /= filename;

                    FileEvent ev;
                    ev.path = fullPath.lexically_normal();

                    if (events & UV_RENAME)
                    {
                        ev.type = FileEventType::Renamed;
                    }
                    else if (events & UV_CHANGE)
                    {
                        ev.type = FileEventType::Modified;
                    }

                    ev.timeSinceEpoch = Zero::Time::GetTimeMs();

                    FileWatcher::Get().m_Impl->m_eventBuffer.enqueue(ev);
                }, pathStr.c_str(), UV_FS_EVENT_RECURSIVE);

                if (rc != 0)
                {
                    uv_close(reinterpret_cast<uv_handle_t*>(&info->handle), [](uv_handle_t* h) {
                        delete static_cast<Impl::WatchInfo*>(h->data);
                    });
                    continue;
                }

                m_Impl->m_activeWatches[pathStr] = std::unique_ptr<Impl::WatchInfo>(info);
            }
            else if (cmd.type == Impl::CommandType::UnWatch)
            {
                auto it = m_Impl->m_activeWatches.find(pathStr);
                if (it != m_Impl->m_activeWatches.end())
                {
                    it->second->refCount--;
                    if (it->second->refCount <= 0)
                    {
                        auto* rawInfo = it->second.release();
                        uv_close(reinterpret_cast<uv_handle_t*>(&rawInfo->handle), [](uv_handle_t* h) {
                            delete static_cast<Impl::WatchInfo*>(h->data);
                        });
                        m_Impl->m_activeWatches.erase(it);
                    }
                }
            }
            else if (cmd.type == Impl::CommandType::UnWatchAll)
            {
                for (auto& [p, info] : m_Impl->m_activeWatches)
                {
                    auto* rawInfo = info.release();
                    uv_close(reinterpret_cast<uv_handle_t*>(&rawInfo->handle), [](uv_handle_t* h) {
                        delete static_cast<Impl::WatchInfo*>(h->data);
                    });
                }
                m_Impl->m_activeWatches.clear();
            }
        }
    }
}
