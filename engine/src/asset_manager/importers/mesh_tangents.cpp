#include "mesh_tangents.hpp"

#include <cmath>

#include <mikktspace.h>

namespace crimson
{
    namespace
    {
        MeshVertex& Corner(const SMikkTSpaceContext* context, int face, int vertex)
        {
            auto& corners = *static_cast<std::span<MeshVertex>*>(context->m_pUserData);
            return corners[static_cast<size_t>(face) * 3 + static_cast<size_t>(vertex)];
        }

        int GetNumFaces(const SMikkTSpaceContext* context)
        {
            return static_cast<int>(static_cast<std::span<MeshVertex>*>(context->m_pUserData)->size() / 3);
        }

        int GetNumVerticesOfFace(const SMikkTSpaceContext*, int)
        {
            return 3;
        }

        void GetPosition(const SMikkTSpaceContext* context, float out[], int face, int vertex)
        {
            const glm::vec3& position = Corner(context, face, vertex).Position;
            out[0] = position.x;
            out[1] = position.y;
            out[2] = position.z;
        }

        void GetNormal(const SMikkTSpaceContext* context, float out[], int face, int vertex)
        {
            const glm::vec3& normal = Corner(context, face, vertex).Normal;
            out[0] = normal.x;
            out[1] = normal.y;
            out[2] = normal.z;
        }

        void GetTexCoord(const SMikkTSpaceContext* context, float out[], int face, int vertex)
        {
            const glm::vec2& uv = Corner(context, face, vertex).UV;
            out[0] = uv.x;
            out[1] = uv.y;
        }

        void SetTangent(const SMikkTSpaceContext* context, const float tangent[], float sign, int face, int vertex)
        {
            Corner(context, face, vertex).Tangent = glm::vec4(tangent[0], tangent[1], tangent[2], sign);
        }

        // Any direction across the surface will do, the shader leaves the normal as it is when w is 0
        void SetFlatTangents(std::span<MeshVertex> corners)
        {
            for (MeshVertex& corner : corners)
            {
                const glm::vec3 axis = std::abs(corner.Normal.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
                const glm::vec3 across = axis - corner.Normal * glm::dot(corner.Normal, axis);
                const float length = glm::length(across);

                corner.Tangent = glm::vec4(length > 0.0f ? across / length : axis, 0.0f);
            }
        }
    }

    void ComputeTangents(std::span<MeshVertex> corners, bool hasUVs)
    {
        if (!hasUVs || corners.size() < 3)
        {
            SetFlatTangents(corners);
            return;
        }

        SMikkTSpaceInterface callbacks{};
        callbacks.m_getNumFaces = GetNumFaces;
        callbacks.m_getNumVerticesOfFace = GetNumVerticesOfFace;
        callbacks.m_getPosition = GetPosition;
        callbacks.m_getNormal = GetNormal;
        callbacks.m_getTexCoord = GetTexCoord;
        callbacks.m_setTSpaceBasic = SetTangent;

        SMikkTSpaceContext context{};
        context.m_pInterface = &callbacks;
        context.m_pUserData = &corners;

        // It only fails when it can't allocate
        if (!genTangSpaceDefault(&context))
            SetFlatTangents(corners);
    }
}
