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

void ProcessMesh(const tinygltf::Model &model, const tinygltf::Mesh &mesh, std::vector<Model::Vertex> &vertices,
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

        // Optional: Access tangents
        const auto tangentIter = primitive.attributes.find("TANGENT");
        const float *tangentData = nullptr;
        if (tangentIter != primitive.attributes.end())
        {
            const auto &tangentAccessor = model.accessors[tangentIter->second];
            const auto &tangentBufferView = model.bufferViews[tangentAccessor.bufferView];
            const auto &tangentBuffer = model.buffers[tangentBufferView.buffer];
            tangentData = reinterpret_cast<const float *>(tangentBuffer.data.data() + tangentBufferView.byteOffset +
                                                          tangentAccessor.byteOffset);
        }

        // Optional: Access texture coordinates
        const auto texCoord0Iter = primitive.attributes.find("TEXCOORD_0");
        const float *texCoord0Data = nullptr;
        if (texCoord0Iter != primitive.attributes.end())
        {
            const auto &texCoordAccessor = model.accessors[texCoord0Iter->second];
            const auto &texCoordBufferView = model.bufferViews[texCoordAccessor.bufferView];
            const auto &texCoordBuffer = model.buffers[texCoordBufferView.buffer];
            texCoord0Data = reinterpret_cast<const float *>(texCoordBuffer.data.data() + texCoordBufferView.byteOffset +
                                                            texCoordAccessor.byteOffset);
        }

        const auto texCoord1Iter = primitive.attributes.find("TEXCOORD_1");
        const float *texCoord1Data = nullptr;
        if (texCoord1Iter != primitive.attributes.end())
        {
            const auto &texCoordAccessor = model.accessors[texCoord1Iter->second];
            const auto &texCoordBufferView = model.bufferViews[texCoordAccessor.bufferView];
            const auto &texCoordBuffer = model.buffers[texCoordBufferView.buffer];
            texCoord1Data = reinterpret_cast<const float *>(texCoordBuffer.data.data() + texCoordBufferView.byteOffset +
                                                            texCoordAccessor.byteOffset);
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

        // Copy vertex data into Vertex struct
        for (size_t i = 0; i < positionAccessor.count; ++i)
        {
            Model::Vertex vertex;

            // Position
            vertex.m_position = glm::vec3(positionData[i * 3 + 0], positionData[i * 3 + 1], positionData[i * 3 + 2]);

            // Normal (default to 0, 0, 1 if not provided)
            if (normalData)
            {
                vertex.m_normal = glm::vec3(normalData[i * 3 + 0], normalData[i * 3 + 1], normalData[i * 3 + 2]);
            }
            else
            {
                vertex.m_normal = glm::vec3(0.0f, 0.0f, 1.0f);
            }

            // Tangent (default to 0, 0, 0, 1 if not provided)
            if (tangentData)
            {
                vertex.m_tangent = glm::vec4(tangentData[i * 4 + 0], tangentData[i * 4 + 1], tangentData[i * 4 + 2],
                                             tangentData[i * 4 + 3]);
            }
            else
            {
                vertex.m_tangent = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            }

            // Texture coordinates (default to 0, 0 if not provided)
            if (texCoord0Data)
            {
                vertex.m_texCoord0 = glm::vec2(texCoord0Data[i * 2 + 0], texCoord0Data[i * 2 + 1]);
            }
            else
            {
                vertex.m_texCoord0 = glm::vec2(0.0f, 0.0f);
            }

            if (texCoord1Data)
            {
                vertex.m_texCoord1 = glm::vec2(texCoord1Data[i * 2 + 0], texCoord1Data[i * 2 + 1]);
            }
            else
            {
                vertex.m_texCoord1 = glm::vec2(0.0f, 0.0f);
            }

            // Color (default to white if not provided)
            if (colorData)
            {
                vertex.m_color =
                    glm::vec4(colorData[i * 4 + 0], colorData[i * 4 + 1], colorData[i * 4 + 2], colorData[i * 4 + 3]);
            }
            else
            {
                vertex.m_color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            }

            vertices.push_back(vertex);
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

void ProcessMaterial(const tinygltf::Material &material, std::vector<Model::Material> &materials)
{
    Model::Material mat;
    mat.m_baseColorFactor = glm::make_vec4(material.pbrMetallicRoughness.baseColorFactor.data());
    mat.m_emissiveFactor = glm::make_vec4(material.emissiveFactor.data());
    mat.m_metallicFactor = static_cast<float>(material.pbrMetallicRoughness.metallicFactor);
    mat.m_roughnessFactor = static_cast<float>(material.pbrMetallicRoughness.roughnessFactor);

    mat.m_baseColorTexture = material.pbrMetallicRoughness.baseColorTexture.index;
    mat.m_metallicRoughnessTexture = material.pbrMetallicRoughness.metallicRoughnessTexture.index;
    mat.m_normalTexture = material.normalTexture.index;
    mat.m_emissiveTexture = material.emissiveTexture.index;
    mat.m_occlusionTexture = material.occlusionTexture.index;

    materials.push_back(mat);

    // Print material properties (for debugging, remove later)
    std::cout << "Material " << materials.size() - 1 << ":" << std::endl;
    std::cout << "  Base Color Factor: " << mat.m_baseColorFactor.r << ", " << mat.m_baseColorFactor.g << ", "
              << mat.m_baseColorFactor.b << ", " << mat.m_baseColorFactor.a << std::endl;
    std::cout << "  Emissive Factor: " << mat.m_emissiveFactor.r << ", " << mat.m_emissiveFactor.g << ", "
              << mat.m_emissiveFactor.b << ", " << mat.m_emissiveFactor.a << std::endl;
    std::cout << "  Metallic Factor: " << mat.m_metallicFactor << std::endl;
    std::cout << "  Roughness Factor: " << mat.m_roughnessFactor << std::endl;
    std::cout << "  Base Color Texture: " << mat.m_baseColorTexture << std::endl;
    std::cout << "  Metallic-Roughness Texture: " << mat.m_metallicRoughnessTexture << std::endl;
    std::cout << "  Normal Texture: " << mat.m_normalTexture << std::endl;
    std::cout << "  Emissive Texture: " << mat.m_emissiveTexture << std::endl;
    std::cout << "  Occlusion Texture: " << mat.m_occlusionTexture << std::endl;
    std::cout << "--------------------------------" << std::endl;
}

void ProcessImage(const tinygltf::Image &image, const std::string &basePath, std::vector<Model::Texture> &textures)
{

    Model::Texture texture;
    texture.m_name = image.name;
    texture.m_width = image.width;
    texture.m_height = image.height;
    texture.m_components = image.component;
    texture.m_mimeType = image.mimeType;

    if (!image.image.empty())
    {
        // Image data is embedded
        texture.m_data = image.image;
    }
    else if (!image.uri.empty())
    {
        // Image data is external, load it using stb_image
        std::string imagePath = basePath + "/" + image.uri;
        int width, height, components;
        unsigned char *data = stbi_load(imagePath.c_str(), &width, &height, &components, 0);
        if (data)
        {
            texture.m_width = width;
            texture.m_height = height;
            texture.m_components = components;
            texture.m_data = std::vector<uint8_t>(data, data + (width * height * components));
            stbi_image_free(data);
        }
        else
        {
            std::cerr << "Failed to load image: " << imagePath << std::endl;
        }
    }
    else
    {
        std::cerr << "Warning: Texture " << texture.m_name << " has no valid image source." << std::endl;
    }

    textures.push_back(texture);
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
    m_materials.clear();           // Clear the material data

    const std::string basePath = filename.substr(0, filename.find_last_of("/"));

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

        for (const auto &material : model.materials)
        {
            ProcessMaterial(material, m_materials);
        }

        for (const auto &image : model.images)
        {
            ProcessImage(image, basePath, m_textures);
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

void Model::Update(float deltaTime, bool animate)
{
    if (animate)
    {
        m_rotationAngle += deltaTime; // Increment the rotation angle
        if (m_rotationAngle > 2.0f * PI)
        {
            m_rotationAngle -= 2.0f * PI; // Keep the angle within [0, 2π]
        }
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

const std::vector<Model::Vertex> &Model::GetVertices() const noexcept
{
    return m_vertices;
}

const std::vector<uint32_t> &Model::GetIndices() const noexcept
{
    return m_indices;
}

const std::vector<Model::Material> &Model::GetMaterials() const noexcept
{
    return m_materials;
}

const std::vector<Model::Texture> &Model::GetTextures() const noexcept
{
    return m_textures;
}