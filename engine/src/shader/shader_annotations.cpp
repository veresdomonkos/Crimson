#include "crimson/renderer/shader_compiler.hpp"

#include <charconv>
#include <cctype>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "crimson/core/log.hpp"

namespace crimson
{
    namespace
    {
        // "float Roughness;" and "Texture2D NormalMap : register(t1, space1);" both end their name before these
        std::string_view DeclaredName(std::string_view code)
        {
            code = code.substr(0, code.find_first_of(":;[="));

            while (!code.empty() && std::isspace(static_cast<unsigned char>(code.back())))
                code.remove_suffix(1);

            const size_t start = code.find_last_of(" \t");
            return start == std::string_view::npos ? std::string_view{} : code.substr(start + 1);
        }

        std::optional<float> ParseFloat(const std::string& word)
        {
            float value = 0.0f;
            const char* end = word.data() + word.size();
            const auto [parsed, error] = std::from_chars(word.data(), end, value);

            if (error != std::errc{} || parsed != end)
                return std::nullopt;

            return value;
        }

        void ParseTags(std::string_view comment, std::string_view name, ShaderAnnotation& annotation)
        {
            std::istringstream stream{ std::string(comment) };
            std::string tag;
            stream >> tag;

            while (!tag.empty())
            {
                std::vector<std::string> words;
                std::string word;

                while (stream >> word && !word.starts_with('@'))
                    words.push_back(word);

                std::vector<float> numbers;
                for (const std::string& text : words)
                {
                    if (const std::optional<float> number = ParseFloat(text))
                        numbers.push_back(*number);
                }

                const bool allNumbers = numbers.size() == words.size();

                if (tag == "@default" && words.size() == 1 && words[0] == "normal")
                    annotation.Texture = ShaderTextureDefault::Normal;
                else if (tag == "@default" && allNumbers && !numbers.empty())
                    annotation.Default = numbers;
                else if (tag == "@range" && allNumbers && numbers.size() == 2)
                    annotation.Range = std::pair{ numbers[0], numbers[1] };
                else
                    LOG_WARN("[Shader] '{}' has an annotation that can't be understood: {}", name, tag);

                tag = word.starts_with('@') ? word : std::string{};
            }
        }
    }

    std::unordered_map<std::string, ShaderAnnotation> ParseShaderAnnotations(std::string_view source)
    {
        std::unordered_map<std::string, ShaderAnnotation> result;

        while (!source.empty())
        {
            const size_t lineEnd = source.find('\n');
            const std::string_view line = source.substr(0, lineEnd);
            source = lineEnd == std::string_view::npos ? std::string_view{} : source.substr(lineEnd + 1);

            const size_t commentStart = line.find("//");
            if (commentStart == std::string_view::npos)
                continue;

            std::string_view comment = line.substr(commentStart + 2);
            const size_t firstTag = comment.find('@');
            if (firstTag == std::string_view::npos)
                continue;

            const std::string_view name = DeclaredName(line.substr(0, commentStart));
            if (name.empty())
                continue;

            comment.remove_prefix(firstTag);
            ParseTags(comment, name, result[std::string(name)]);
        }

        return result;
    }
}
