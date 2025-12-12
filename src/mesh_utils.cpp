// Standard Library Headers
#include <iostream>

// Project Headers
#include "mesh_utils.h"
#include "mikktspace.h"

//----------------------------------------------------------------------
// Internal Types and Utility Functions

namespace {

// User data structure for MikkTSpace
struct MeshData {
    std::vector<Model::Vertex> *m_vertices;
    std::vector<uint32_t> *m_indices;
    uint32_t m_firstIndex = 0;
    uint32_t m_indexCount = 0;
};

// Returns the number of faces (triangles)
int getNumFaces(const SMikkTSpaceContext *pContext) {
    MeshData *mesh = static_cast<MeshData *>(pContext->m_pUserData);
    return static_cast<int>(mesh->m_indexCount / 3);
}

// Each face in a triangle mesh always has 3 vertices
int getNumVerticesOfFace(const SMikkTSpaceContext *, const int) {
    return 3;
}

// Provides the position of the vertex for the given face and vertex index
void getPosition(const SMikkTSpaceContext *pContext, float position[3], const int face,
                 const int vert) {
    MeshData *mesh = static_cast<MeshData *>(pContext->m_pUserData);
    int index = (*mesh->m_indices)[mesh->m_firstIndex + face * 3 + vert];
    const Model::Vertex& vertex = (*mesh->m_vertices)[index];
    position[0] = vertex.m_position.x;
    position[1] = vertex.m_position.y;
    position[2] = vertex.m_position.z;
}

// Provides the normal of the vertex
void getNormal(const SMikkTSpaceContext *pContext, float normal[3], const int face,
               const int vert) {
    MeshData *mesh = static_cast<MeshData *>(pContext->m_pUserData);
    int index = (*mesh->m_indices)[mesh->m_firstIndex + face * 3 + vert];
    const Model::Vertex& vertex = (*mesh->m_vertices)[index];
    normal[0] = vertex.m_normal.x;
    normal[1] = vertex.m_normal.y;
    normal[2] = vertex.m_normal.z;
}

// Provides the texture coordinate of the vertex
void getTexCoord(const SMikkTSpaceContext *pContext, float texCoord[2], const int face,
                 const int vert) {
    MeshData *mesh = static_cast<MeshData *>(pContext->m_pUserData);
    int index = (*mesh->m_indices)[mesh->m_firstIndex + face * 3 + vert];
    const Model::Vertex& vertex = (*mesh->m_vertices)[index];
    texCoord[0] = vertex.m_texCoord0.x;
    texCoord[1] = vertex.m_texCoord0.y;
}

// Called to set the computed tangent (and its sign for handedness)
void setTSpaceBasic(const SMikkTSpaceContext *pContext, const float tangent[3], const float sign,
                    const int face, const int vert) {
    // Fetch the vertex data
    MeshData *mesh = static_cast<MeshData *>(pContext->m_pUserData);
    int index = (*mesh->m_indices)[mesh->m_firstIndex + face * 3 + vert];
    Model::Vertex& vertex = (*mesh->m_vertices)[index];

    float n[3];
    pContext->m_pInterface->m_getNormal(pContext, n, face, vert);
    glm::vec3 normal(n[0], n[1], n[2]);

    // Normalize the computed tangent vector
    glm::vec3 t = glm::normalize(glm::vec3(tangent[0], tangent[1], tangent[2]));

    // Check if the computed tangent is sufficiently orthogonal to the normal
    if (glm::abs(glm::dot(t, normal)) < 0.9f) {
        vertex.m_tangent = glm::vec4(tangent[0], tangent[1], tangent[2], -sign);
    } else {
        // Generate a fallback tangent that is orthogonal to the normal.
        // Use a threshold to handle the singular case when the normal is nearly (0, 0, -1)
        constexpr float kSingularityThreshold = -0.99998796f;
        if (normal.z < kSingularityThreshold) {
            vertex.m_tangent = glm::vec4(0.0f, -1.0f, 0.0f, 1.0f);
        } else {
            float a = 1.0f / (1.0f + normal.z);
            float b = -normal.x * normal.y * a;
            vertex.m_tangent = glm::vec4(1.0f - normal.x * normal.x * a, b, -normal.x, 1.0f);
        }
    }
}

} // namespace

//----------------------------------------------------------------------

namespace mesh_utils {
void GenerateTangents(const Model::SubMesh& subMesh, std::vector<Model::Vertex>& vertices,
                      std::vector<uint32_t>& indices) {
    // Set up the MikkTSpace interface / function pointers
    SMikkTSpaceInterface interface {};
    interface.m_getNumFaces = getNumFaces;
    interface.m_getNumVerticesOfFace = getNumVerticesOfFace;
    interface.m_getPosition = getPosition;
    interface.m_getNormal = getNormal;
    interface.m_getTexCoord = getTexCoord;
    interface.m_setTSpaceBasic = setTSpaceBasic;

    // Prepare the context
    SMikkTSpaceContext context;
    MeshData meshData = {&vertices, &indices, subMesh.m_firstIndex, subMesh.m_indexCount};
    context.m_pUserData = &meshData;
    context.m_pInterface = &interface;

    if (!genTangSpaceDefault(&context)) {
        std::cerr << "Failed to generate tangents!" << std::endl;
    }
}

} // namespace mesh_utils