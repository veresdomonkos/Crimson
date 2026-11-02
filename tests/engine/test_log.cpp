#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include <crimson/core/log.hpp>

using namespace crimson;

static bool Contains(const std::vector<std::string>& messages, const std::string& message)
{
    return std::ranges::find(messages, message) != messages.end();
}

TEST_CASE("A subscriber first gets the history, then new messages", "[log]")
{
    LOG_INFO("before subscribing");

    std::vector<std::string> received;
    const uint64_t id = Logger::Subscribe([&](LogLevel, const std::string& message) { received.push_back(message); });

    CHECK(Contains(received, "before subscribing"));

    LOG_WARN("after subscribing");
    CHECK(Contains(received, "after subscribing"));

    Logger::Unsubscribe(id);
}

// A panel that was destroyed without this would be called through a dangling pointer
TEST_CASE("An unsubscribed callback is never called again", "[log]")
{
    int calls = 0;
    const uint64_t id = Logger::Subscribe([&](LogLevel, const std::string&) { ++calls; });

    Logger::Unsubscribe(id);
    calls = 0;

    LOG_ERROR("nobody is listening to this");
    CHECK(calls == 0);
}

TEST_CASE("Unsubscribing one callback leaves the others", "[log]")
{
    int first = 0;
    int second = 0;

    const uint64_t firstId = Logger::Subscribe([&](LogLevel, const std::string&) { ++first; });
    const uint64_t secondId = Logger::Subscribe([&](LogLevel, const std::string&) { ++second; });

    CHECK(firstId != secondId);

    Logger::Unsubscribe(firstId);
    first = 0;
    second = 0;

    LOG_INFO("one is still listening");

    CHECK(first == 0);
    CHECK(second == 1);

    Logger::Unsubscribe(secondId);
}
