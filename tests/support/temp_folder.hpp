#pragma once
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include <chrono>

namespace crimson::tests
{
    // A folder that only exists for one test
    class TempFolder
    {
    public:
        TempFolder()
        {
            static std::atomic<int> s_counter = 0;
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();

            m_path = std::filesystem::temp_directory_path() / ("crimson_test_" + std::to_string(stamp) + "_" + std::to_string(s_counter++));
            std::filesystem::create_directories(m_path);
        }

        ~TempFolder()
        {
            std::error_code error;
            std::filesystem::remove_all(m_path, error);
        }

        TempFolder(const TempFolder&) = delete;
        TempFolder& operator=(const TempFolder&) = delete;

        [[nodiscard]] const std::filesystem::path& Path() const { return m_path; }

        // Creates the folders on the way too
        std::filesystem::path Write(const std::filesystem::path& relative, std::string_view contents) const
        {
            const std::filesystem::path file = m_path / relative;
            std::filesystem::create_directories(file.parent_path());

            std::ofstream stream(file, std::ios::binary);
            stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            return file;
        }
    private:
        std::filesystem::path m_path;
    };
}
