#include "crimson_editor/system_shell.hpp"

#include <array>

namespace fs = std::filesystem;

namespace crimson::editor
{
    std::string FileUri(const fs::path& path)
    {
        static constexpr std::array<char, 16> kHex = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

        const std::u8string utf8 = path.generic_u8string();

        // A path with a drive letter, such as C:/, still needs the slash that starts the path part of the URI
        std::string uri = "file://";
        if (utf8.empty() || utf8.front() != u8'/')
            uri += '/';

        for (const char8_t c : utf8)
        {
            const auto byte = static_cast<unsigned char>(c);
            const bool plain = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9')
                || byte == '-' || byte == '.' || byte == '_' || byte == '~' || byte == '/';

            if (plain)
            {
                uri += static_cast<char>(byte);
            }
            else
            {
                uri += '%';
                uri += kHex[byte >> 4];
                uri += kHex[byte & 0x0F];
            }
        }

        return uri;
    }

    std::string PathToUtf8(const fs::path& path)
    {
        const std::u8string text = path.u8string();
        return std::string(text.begin(), text.end());
    }

    // How CommandLineToArgvW reads a command line back: backslashes are only special in front of a quote
    std::string QuoteWindowsArgument(const std::string& argument)
    {
        if (!argument.empty() && argument.find_first_of(" \t\n\v\"") == std::string::npos)
            return argument;

        std::string quoted = "\"";
        size_t backslashes = 0;

        for (const char c : argument)
        {
            if (c == '\\')
            {
                ++backslashes;
                continue;
            }

            // Backslashes in front of a quote are doubled, and the quote gets one more to be taken as it is
            if (c == '"')
                quoted.append(backslashes * 2 + 1, '\\');
            else
                quoted.append(backslashes, '\\');

            quoted += c;
            backslashes = 0;
        }

        // Backslashes at the end come before the closing quote, so they are doubled too
        quoted.append(backslashes * 2, '\\');
        quoted += '"';
        return quoted;
    }
}
