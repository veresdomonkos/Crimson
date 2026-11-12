#pragma once
#include <span>

#include "crimson/renderer/mesh.hpp"

namespace crimson
{
    // Fills in the tangents of an unindexed triangle list, three corners per triangle. With UVs they are MikkTSpace's,
    // which is what normal maps are baked against. Without, each gets one perpendicular to its normal and w = 0.
    void ComputeTangents(std::span<MeshVertex> corners, bool hasUVs);
}
