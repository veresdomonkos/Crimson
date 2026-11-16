#pragma once
#include <condition_variable>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace crimson::editor
{
    // Copies of unsaved work, kept to restore after the editor didn't close normally. The callers name the files. They
    // are written on a thread of their own, so a big copy doesn't stall a frame. From the main thread, everything
    // behaves as if the writes were synchronous: asking about a file waits for its write.
    class RecoveryFiles
    {
    public:
        explicit RecoveryFiles(std::filesystem::path folder);

        // Writes what is still waiting. A destroyed session never discards a copy, only Remove does.
        ~RecoveryFiles();

        RecoveryFiles(const RecoveryFiles&) = delete;
        RecoveryFiles& operator=(const RecoveryFiles&) = delete;

        // Returns at once. A newer write of the same name replaces one that is still waiting.
        void Write(std::string name, std::string contents);

        // A file that isn't there is fine
        void Remove(const std::string& name);

        // When the file was last written, or nothing if it isn't there
        [[nodiscard]] std::optional<std::filesystem::file_time_type> WrittenAt(const std::string& name);

        [[nodiscard]] std::optional<std::string> Read(const std::string& name);

        [[nodiscard]] const std::filesystem::path& Folder() const { return m_folder; }
    private:
        void Work();

        // Until the file's write, waiting or in progress, is done
        void WaitFor(std::unique_lock<std::mutex>& lock, const std::string& name);
    private:
        std::filesystem::path m_folder;

        std::mutex m_mutex;
        std::condition_variable m_changed;
        std::map<std::string, std::string> m_waiting; // name to contents
        std::string m_writing;                        // the name being written, empty while idle
        bool m_stopping = false;

        // Last, so that it starts once the rest exists
        std::thread m_worker;
    };
}
