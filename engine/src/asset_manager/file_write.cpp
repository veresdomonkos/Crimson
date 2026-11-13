#include "file_write.hpp"

#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace crimson
{
    bool WriteFileSafely(const fs::path& file, std::string_view contents)
    {
        fs::path temporary = file;
        temporary += ".saving";

        {
            std::ofstream stream(temporary, std::ios::binary);
            stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            stream.close();

            if (!stream)
            {
                std::error_code removeError;
                fs::remove(temporary, removeError);
                return false;
            }
        }

        std::error_code error;
        fs::rename(temporary, file, error);

        if (error)
        {
            fs::remove(temporary, error);
            return false;
        }

        return true;
    }
}
