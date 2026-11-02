set(CRIMSON_EMBED_FILE_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/EmbedFile.cmake")

# Bundles every file under DIRECTORY into the target, and generates the function that lists them:
#   std::span<const crimson::EmbeddedFile> NAMESPACE::FUNCTION();
# Each file gets its own generated source, so editing one only rebuilds that one.
function(crimson_embed_resources target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "DIRECTORY;NAMESPACE;FUNCTION" "")

    get_filename_component(root "${ARG_DIRECTORY}" ABSOLUTE)
    set(outputDir "${CMAKE_CURRENT_BINARY_DIR}/embedded_${ARG_FUNCTION}")

    file(GLOB_RECURSE files CONFIGURE_DEPENDS "${root}/*")

    set(generated "")
    set(declarations "")
    set(entries "")

    foreach(file IN LISTS files)
        file(RELATIVE_PATH relative "${root}" "${file}")
        string(MAKE_C_IDENTIFIER "embedded_${relative}" symbol)
        set(output "${outputDir}/${symbol}.cpp")

        add_custom_command(
                OUTPUT "${output}"
                COMMAND "${CMAKE_COMMAND}"
                    "-DINPUT=${file}" "-DOUTPUT=${output}" "-DSYMBOL=${symbol}" "-DNAMESPACE=${ARG_NAMESPACE}"
                    -P "${CRIMSON_EMBED_FILE_SCRIPT}"
                DEPENDS "${file}" "${CRIMSON_EMBED_FILE_SCRIPT}"
                COMMENT "Embedding ${relative}"
                VERBATIM
        )

        list(APPEND generated "${output}")
        string(APPEND declarations "    extern const unsigned char ${symbol}[];\n    extern const std::size_t ${symbol}_size;\n")
        string(APPEND entries "            { \"${relative}\", { reinterpret_cast<const std::byte*>(${symbol}), ${symbol}_size } },\n")
    endforeach()

    set(table "${outputDir}/table.cpp")
    file(CONFIGURE OUTPUT "${table}" @ONLY CONTENT
"#include <crimson/core/embedded_resource.hpp>

namespace @ARG_NAMESPACE@
{
@declarations@
    std::span<const crimson::EmbeddedFile> @ARG_FUNCTION@()
    {
        static const crimson::EmbeddedFile files[] = {
@entries@        };
        return files;
    }
}
")

    target_sources(${target} PRIVATE ${generated} "${table}")
    set_source_files_properties(${generated} "${table}" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
endfunction()
