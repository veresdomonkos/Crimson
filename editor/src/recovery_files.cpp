#include "crimson_editor/recovery_files.hpp"

#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include <crimson/core/file_write.hpp>
#include <crimson/core/log.hpp>

namespace fs = std::filesystem;

namespace crimson::editor
{
    RecoveryFiles::RecoveryFiles(fs::path folder)
        : m_folder(std::move(folder)), m_worker([this] { Work(); })
    {
    }

    RecoveryFiles::~RecoveryFiles()
    {
        {
            const std::lock_guard lock(m_mutex);
            m_stopping = true;
        }
        m_changed.notify_all();
        m_worker.join();
    }

    void RecoveryFiles::Write(std::string name, std::string contents)
    {
        {
            const std::lock_guard lock(m_mutex);
            m_waiting.insert_or_assign(std::move(name), std::move(contents));
        }
        m_changed.notify_all();
    }

    void RecoveryFiles::Remove(const std::string& name)
    {
        std::unique_lock lock(m_mutex);
        m_waiting.erase(name);
        WaitFor(lock, name);

        std::error_code error;
        fs::remove(m_folder / name, error);
    }

    std::optional<fs::file_time_type> RecoveryFiles::WrittenAt(const std::string& name)
    {
        std::unique_lock lock(m_mutex);
        WaitFor(lock, name);

        std::error_code error;
        const fs::file_time_type time = fs::last_write_time(m_folder / name, error);
        if (error)
            return std::nullopt;
        return time;
    }

    std::optional<std::string> RecoveryFiles::Read(const std::string& name)
    {
        std::unique_lock lock(m_mutex);
        WaitFor(lock, name);

        std::ifstream stream(m_folder / name, std::ios::binary);
        if (!stream)
            return std::nullopt;
        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    }

    void RecoveryFiles::Work()
    {
        std::unique_lock lock(m_mutex);
        while (true)
        {
            m_changed.wait(lock, [this] { return !m_waiting.empty() || m_stopping; });
            if (m_waiting.empty())
                return; // stopping, with nothing left to write

            auto next = m_waiting.extract(m_waiting.begin());
            m_writing = next.key();
            lock.unlock();

            std::error_code error;
            fs::create_directories(m_folder, error);
            if (!WriteFileSafely(m_folder / next.key(), next.mapped()))
                LOG_WARN("[Recovery] Couldn't write {}", (m_folder / next.key()).string());

            lock.lock();
            m_writing.clear();
            m_changed.notify_all();
        }
    }

    void RecoveryFiles::WaitFor(std::unique_lock<std::mutex>& lock, const std::string& name)
    {
        m_changed.wait(lock, [&] { return !m_waiting.contains(name) && m_writing != name; });
    }
}
