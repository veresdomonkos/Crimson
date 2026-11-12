#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "asset_manager/importers/mesh_tangents.hpp"

using namespace crimson;
using Catch::Approx;

namespace
{
    // A unit square in the XY plane facing +Z, as two triangles, with the UV of each corner from uvOf(x, y)
    template <typename UvOf>
    std::vector<MeshVertex> Square(UvOf uvOf)
    {
        const glm::vec2 corners[] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 0 }, { 1, 1 }, { 0, 1 } };

        std::vector<MeshVertex> vertices;
        for (const glm::vec2& corner : corners)
            vertices.push_back({ .Position = glm::vec3(corner, 0.0f), .Normal = glm::vec3(0.0f, 0.0f, 1.0f), .UV = uvOf(corner.x, corner.y) });
        return vertices;
    }

    void CheckTangent(const MeshVertex& vertex, const glm::vec3& expected, float side)
    {
        CHECK(vertex.Tangent.x == Approx(expected.x).margin(1e-4));
        CHECK(vertex.Tangent.y == Approx(expected.y).margin(1e-4));
        CHECK(vertex.Tangent.z == Approx(expected.z).margin(1e-4));
        CHECK(vertex.Tangent.w == side);
    }
}

TEST_CASE("A tangent points along +U, with the bitangent along +V", "[mesh tangents]")
{
    std::vector<MeshVertex> square = Square([](float x, float y) { return glm::vec2(x, y); });
    ComputeTangents(square, true);

    for (const MeshVertex& vertex : square)
        CheckTangent(vertex, glm::vec3(1.0f, 0.0f, 0.0f), 1.0f);
}

TEST_CASE("Mirrored UVs flip the tangent and the bitangent's side", "[mesh tangents]")
{
    std::vector<MeshVertex> square = Square([](float x, float y) { return glm::vec2(1.0f - x, y); });
    ComputeTangents(square, true);

    for (const MeshVertex& vertex : square)
        CheckTangent(vertex, glm::vec3(-1.0f, 0.0f, 0.0f), -1.0f);
}

TEST_CASE("Without UVs a tangent only lies across the surface, and says there is nothing to go by", "[mesh tangents]")
{
    std::vector<MeshVertex> square = Square([](float, float) { return glm::vec2(0.0f); });
    ComputeTangents(square, false);

    for (const MeshVertex& vertex : square)
    {
        CHECK(vertex.Tangent.w == 0.0f);
        CHECK(glm::dot(glm::vec3(vertex.Tangent), vertex.Normal) == Approx(0.0f).margin(1e-6));
        CHECK(glm::length(glm::vec3(vertex.Tangent)) == Approx(1.0f));
    }
}
