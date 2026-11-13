#pragma once
#include <string_view>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include <crimson/asset_manager/asset_manager.hpp>

namespace crimson::tests
{
    // What the editor does after a mesh renderer's assets changed, through the registry
    inline void ResolveAssets(AssetManager& assets, MeshRendererComponent& renderer)
    {
        const ComponentType* type = assets.Components().Find<MeshRendererComponent>();
        REQUIRE(type);
        ResolveComponentAssets(*type, &renderer, assets);
    }

    // A field of one of a node's components, as a file has it or as the scene was captured
    template <typename T>
    const T& FieldOf(const AssetManager& assets, const SceneNode& node, std::string_view type, std::string_view key)
    {
        const SceneComponent* component = node.FindComponent(type);
        REQUIRE(component);

        const FieldValue* value = assets.Components().FieldOf(*component, key);
        REQUIRE(value);
        REQUIRE(std::holds_alternative<T>(*value));
        return std::get<T>(*value);
    }
}
