// Standard Library Headers
#include <iostream>

// Third-Party Library Headers
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RIGHT_HANDED
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <glm/gtx/polar_coordinates.hpp>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

// Project Headers
#include "model.h"

//----------------------------------------------------------------------
// Internal Constants and Utility Functions

namespace
{

// Constants
constexpr float PI = 3.14159265358979323846f;

void ProcessMesh(const tinygltf::Model &model, const tinygltf::Mesh &mesh, std::vector<float> &vertices,
                 std::vector<uint32_t> &indices)
{
    for (const auto &primitive : mesh.primitives)
    {
        // Access vertex positions
        const auto &positionAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
        const auto &positionBufferView = model.bufferViews[positionAccessor.bufferView];
        const auto &positionBuffer = model.buffers[positionBufferView.buffer];
        const float *positionData = reinterpret_cast<const float *>(
            positionBuffer.data.data() + positionBufferView.byteOffset + positionAccessor.byteOffset);

        // Optional: Access vertex normals
        const auto normalIter = primitive.attributes.find("NORMAL");
        const float *normalData = nullptr;
        if (normalIter != primitive.attributes.end())
        {
            const auto &normalAccessor = model.accessors[normalIter->second];
            const auto &normalBufferView = model.bufferViews[normalAccessor.bufferView];
            const auto &normalBuffer = model.buffers[normalBufferView.buffer];
            normalData = reinterpret_cast<const float *>(normalBuffer.data.data() + normalBufferView.byteOffset +
                                                         normalAccessor.byteOffset);
        }

        // Optional: Access vertex colors
        const auto colorIter = primitive.attributes.find("COLOR_0");
        const float *colorData = nullptr;
        if (colorIter != primitive.attributes.end())
        {
            const auto &colorAccessor = model.accessors[colorIter->second];
            const auto &colorBufferView = model.bufferViews[colorAccessor.bufferView];
            const auto &colorBuffer = model.buffers[colorBufferView.buffer];
            colorData = reinterpret_cast<const float *>(colorBuffer.data.data() + colorBufferView.byteOffset +
                                                        colorAccessor.byteOffset);
        }

        // Copy vertex data (positions, normals, colors)
        for (size_t i = 0; i < positionAccessor.count; ++i)
        {
            // Position
            vertices.push_back(positionData[i * 3 + 0]); // x
            vertices.push_back(positionData[i * 3 + 1]); // y
            vertices.push_back(positionData[i * 3 + 2]); // z

            // Normal (default to 0, 0, 1 if not provided)
            if (normalData)
            {
                vertices.push_back(normalData[i * 3 + 0]); // nx
                vertices.push_back(normalData[i * 3 + 1]); // ny
                vertices.push_back(normalData[i * 3 + 2]); // nz
            }
            else
            {
                vertices.push_back(0.0f); // nx
                vertices.push_back(0.0f); // ny
                vertices.push_back(1.0f); // nz
            }

            // Color (default to white if not provided)
            if (colorData)
            {
                vertices.push_back(colorData[i * 3 + 0]); // r
                vertices.push_back(colorData[i * 3 + 1]); // g
                vertices.push_back(colorData[i * 3 + 2]); // b
            }
            else
            {
                vertices.push_back(1.0f); // r
                vertices.push_back(1.0f); // g
                vertices.push_back(1.0f); // b
            }
        }

        // Access indices (if present)
        if (primitive.indices >= 0)
        {
            const auto &indexAccessor = model.accessors[primitive.indices];
            const auto &indexBufferView = model.bufferViews[indexAccessor.bufferView];
            const auto &indexBuffer = model.buffers[indexBufferView.buffer];
            const void *indexData = indexBuffer.data.data() + indexBufferView.byteOffset + indexAccessor.byteOffset;

            if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
            {
                const uint16_t *data = reinterpret_cast<const uint16_t *>(indexData);
                for (size_t i = 0; i < indexAccessor.count; ++i)
                {
                    indices.push_back(static_cast<uint32_t>(data[i]));
                }
            }
            else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
            {
                const uint32_t *data = reinterpret_cast<const uint32_t *>(indexData);
                indices.insert(indices.end(), data, data + indexAccessor.count);
            }
        }
    }
}

} // namespace

//----------------------------------------------------------------------
// Model Class Implementation

void Model::LoadModel(const std::string &filename)
{
    m_transform = glm::mat4(1.0f); // Reset the model transformation matrix
    m_rotationAngle = 0.0f;        // Reset the model rotation angle
    m_vertices.clear();            // Clear the vertex data
    m_indices.clear();             // Clear the index data

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    bool result = loader.LoadASCIIFromFile(&model, &err, &warn, filename);

    if (result)
    {
        for (const auto &mesh : model.meshes)
        {
            ProcessMesh(model, mesh, m_vertices, m_indices);
        }

        // Rotation to correct orientation (90 degrees in radians for X-axis)
        float xAxisAngle = PI / 2.0f; // 90 degrees
        m_transform = glm::rotate(glm::mat4(1.0f), xAxisAngle, glm::vec3(1.0f, 0.0f, 0.0f));
    }
    else
    {
        std::cerr << "Failed to load model: " << err << std::endl;
    }
}

void Model::AnimateModel(float deltaTime)
{
    m_rotationAngle += deltaTime; // Increment the rotation angle
    if (m_rotationAngle > 2.0f * PI)
    {
        m_rotationAngle -= 2.0f * PI; // Keep the angle within [0, 2π]
    }

    // Rotation to correct orientation (90 degrees in radians for X-axis)
    float xAxisAngle = PI / 2.0f; // 90 degrees
    glm::mat4 xRotationMatrix = glm::rotate(glm::mat4(1.0f), xAxisAngle, glm::vec3(1.0f, 0.0f, 0.0f));

    // Create the Y-axis rotation matrix (dynamic rotation angle)
    glm::mat4 yRotationMatrix = glm::rotate(glm::mat4(1.0f), -m_rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));

    // Combine the rotations: apply X-axis rotation first, then Y-axis rotation
    m_transform = yRotationMatrix * xRotationMatrix;
}

const glm::mat4 &Model::GetTransform() const noexcept
{
    return m_transform;
}

const std::vector<float> &Model::GetVertices() const noexcept
{
    return m_vertices;
}

const std::vector<uint32_t> &Model::GetIndices() const noexcept
{
    return m_indices;
}