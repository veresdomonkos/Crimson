#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include <crimson/renderer/handle_registry.hpp>
#include <crimson/renderer/resource_handles.hpp>

using namespace crimson;

using Registry = HandleRegistry<TextureHandle, std::string>;

TEST_CASE("A default handle is invalid, and a registered one isn't", "[handles]")
{
    Registry registry;

    CHECK_FALSE(TextureHandle{});
    CHECK(TextureHandle::Invalid() == TextureHandle{});
    CHECK_FALSE(registry.Contains(TextureHandle{}));

    const TextureHandle first = registry.Register("first");

    // The first slot has id 0, the generation is what keeps it apart from an invalid handle
    CHECK(first.GetId() == 0);
    CHECK(first);
    CHECK(registry.Get(first) == "first");
}

TEST_CASE("An unregistered handle stops working, even after its slot is reused", "[handles]")
{
    Registry registry;

    const TextureHandle old = registry.Register("old");
    registry.Unregister(old);

    CHECK_FALSE(registry.Contains(old));
    CHECK(registry.Find(old) == nullptr);

    const TextureHandle reused = registry.Register("new");

    CHECK(reused.GetId() == old.GetId());
    CHECK(reused != old);
    CHECK_FALSE(registry.Contains(old));
    CHECK(registry.Get(reused) == "new");

    SECTION("Unregistering the stale handle leaves the new value alone")
    {
        registry.Unregister(old);
        CHECK(registry.Contains(reused));
    }
}

TEST_CASE("Iterating skips free slots", "[handles]")
{
    Registry registry;

    const TextureHandle a = registry.Register("a");
    const TextureHandle b = registry.Register("b");
    const TextureHandle c = registry.Register("c");
    registry.Unregister(b);

    std::vector<std::string> values;
    for (const std::string& value : registry)
        values.push_back(value);

    CHECK(values == std::vector<std::string>{ "a", "c" });
    CHECK(registry.GetHandles() == std::vector<TextureHandle>{ a, c });
}
