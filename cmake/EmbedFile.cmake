# Writes a file's bytes as a C++ array. Run with: cmake -DINPUT=... -DOUTPUT=... -DSYMBOL=... -DNAMESPACE=... -P EmbedFile.cmake

file(READ "${INPUT}" head LIMIT 24)
if(head MATCHES "^version https://git-lfs/")
    message(FATAL_ERROR "${INPUT} is a Git LFS pointer, not the real file. Run: git lfs pull")
endif()

file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hexLength)
math(EXPR size "${hexLength} / 2")

# Long lines are hard on compilers, so break them up
string(REGEX REPLACE "([0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f])" "\\1\n" hex "${hex}")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")

# The extra zero keeps an empty file's array valid
file(WRITE "${OUTPUT}"
"#include <cstddef>

namespace ${NAMESPACE}
{
    extern const unsigned char ${SYMBOL}[];
    extern const std::size_t ${SYMBOL}_size;

    const unsigned char ${SYMBOL}[] = {
${bytes}0x00
    };
    const std::size_t ${SYMBOL}_size = ${size};
}
")
