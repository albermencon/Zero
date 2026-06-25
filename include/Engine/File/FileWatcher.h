#pragma once
#include <filesystem>
#include <cstdint>
#include <memory>

namespace Zero
{
    enum class FileEventType : uint8_t
    {
        Created,
        Modified,
        Deleted,
        Renamed
    };

    struct FileEvent
    {
        std::filesystem::path path;
        uint64_t timeSinceEpoch;
        FileEventType type;

        bool operator==(const FileEvent& other) const
        {
            return path == other.path && type == other.type && timeSinceEpoch == other.timeSinceEpoch;
        }
        bool operator!=(const FileEvent& other) const
        {
            return !(*this == other);
        }
    };

    using WatchHandle = uint64_t;
    constexpr WatchHandle InvalidWatchHandle = 0;

    class Application;

	class FileWatcher
    {
    public:
        static FileWatcher& Get();

        // Direct function pointer callback signature.
        // Guarantee: Callbacks will never be invoked after UnWatch returns.
        using FileCallback = void(*)(const FileEvent&, void*);

        WatchHandle WatchDirectory(const std::filesystem::path& path, FileCallback callback, void* userData = nullptr);
        WatchHandle WatchFile(const std::filesystem::path& path, FileCallback callback, void* userData = nullptr);
        
        void UnWatch(WatchHandle handle);
        void UnWatchAll();
        
    private:
        friend class Application;
        FileWatcher();
        ~FileWatcher();

        void Init();
        void Shutdown();

        void Poll();
        void WatchLoop();
        void ProcessPendingCommands();

        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
