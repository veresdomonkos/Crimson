#pragma once

#define BIND_FN(fn) [this](auto&&... args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }

namespace crimson
{
    using ResourceHandle = uint64_t;
}