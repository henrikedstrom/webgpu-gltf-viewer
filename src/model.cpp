// Standard Library Headers
#include <iostream>
#include <limits>

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
                 std::vector<uint32_t> &indices, std::vector<Model::SubMesh> &subMeshes, const glm::mat4 &transform)
{
    glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));
    glm::mat3 tangentMatrix = glm::mat3(transform);

    for (const auto &primitive : mesh.primitives)
    {
        if (primitive.material < 0)
        {
            // TODO: Handle this in another way? Assign 'default' material?
            continue;
        }

        Model::SubMesh subMesh;
        subMesh.m_firstIndex = static_cast<uint32_t>(indices.size());
        subMesh.m_materialIndex = primitive.material;

        uint32_t vertexOffset = static_cast<uint32_t>(vertices.size());

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
            glm::vec4 pos = glm::vec4(positionData[i * 3 + 0], positionData[i * 3 + 1], positionData[i * 3 + 2], 1.0f);
            vertex.m_position = glm::vec3(transform * pos);

            // Normal (default to 0, 0, 1 if not provided)
            if (normalData)
            {
                vertex.m_normal = glm::normalize(
                    normalMatrix * glm::vec3(normalData[i * 3 + 0], normalData[i * 3 + 1], normalData[i * 3 + 2]));
            }
            else
            {
                vertex.m_normal = glm::normalize(normalMatrix * glm::vec3(0.0f, 0.0f, 1.0f));
            }

            // Tangent (default to 0, 0, 0, 1 if not provided)
            if (tangentData)
            {
                glm::vec3 transformedTangent =
                    tangentMatrix * glm::vec3(tangentData[i * 4 + 0], tangentData[i * 4 + 1], tangentData[i * 4 + 2]);

                vertex.m_tangent =
                    glm::vec4(glm::normalize(transformedTangent), tangentData[i * 4 + 3]); // Preserve handedness (w)
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

            subMesh.m_indexCount = indexAccessor.count;

            if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
            {
                const uint8_t *data = reinterpret_cast<const uint8_t *>(indexData);
                for (size_t i = 0; i < indexAccessor.count; ++i)
                {
                    indices.push_back(vertexOffset + data[i]);
                }
            }
            else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
            {
                const uint16_t *data = reinterpret_cast<const uint16_t *>(indexData);
                for (size_t i = 0; i < indexAccessor.count; ++i)
                {
                    indices.push_back(vertexOffset + static_cast<uint32_t>(data[i]));
                }
            }
            else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
            {
                const uint32_t *data = reinterpret_cast<const uint32_t *>(indexData);
                for (size_t i = 0; i < indexAccessor.count; ++i)
                {
                    indices.push_back(vertexOffset + data[i]);
                }
            }
            else
            {
                assert(false && "Invalid index accessor component type");
            }
        }
        else
        {
            // Non-indexed mesh: generate sequential indices
            subMesh.m_indexCount = static_cast<uint32_t>(positionAccessor.count);
            for (uint32_t i = 0; i < positionAccessor.count; ++i)
            {
                indices.push_back(vertexOffset + i);
            }
        }

        subMeshes.push_back(subMesh);
    }
}

void ProcessNode(const tinygltf::Model &model, int nodeIndex, const glm::mat4 &parentTransform,
                 std::vector<Model::Vertex> &vertices, std::vector<uint32_t> &indices,
                 std::vector<Model::SubMesh> &subMeshes)
{
    const tinygltf::Node &node = model.nodes[nodeIndex];

    // Compute the local transformation matrix
    glm::mat4 localTransform(1.0f);

    // If the node has a transformation matrix, use it
    if (!node.matrix.empty())
    {
        localTransform = glm::make_mat4(node.matrix.data());
    }
    else
    {
        // Otherwise, compute the transformation from translation, rotation, and scale
        if (!node.translation.empty())
        {
            localTransform = glm::translate(localTransform,
                                            glm::vec3(node.translation[0], node.translation[1], node.translation[2]));
        }
        if (!node.rotation.empty())
        {
            glm::quat rotationQuat = glm::quat(node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);
            localTransform *= glm::mat4_cast(rotationQuat);
        }
        if (!node.scale.empty())
        {
            localTransform = glm::scale(localTransform, glm::vec3(node.scale[0], node.scale[1], node.scale[2]));
        }
    }

    // Combine with parent transform
    glm::mat4 globalTransform = parentTransform * localTransform;

    // If this node has a mesh, process it
    if (node.mesh >= 0)
    {
        const tinygltf::Mesh &mesh = model.meshes[node.mesh];
        ProcessMesh(model, mesh, vertices, indices, subMeshes, globalTransform);
    }

    // Recursively process children nodes
    for (int childIndex : node.children)
    {
        ProcessNode(model, childIndex, globalTransform, vertices, indices, subMeshes);
    }
}

void ProcessMaterial(const tinygltf::Material &material, std::vector<Model::Material> &materials)
{
    Model::Material mat;
    mat.m_baseColorFactor = glm::make_vec4(material.pbrMetallicRoughness.baseColorFactor.data());
    mat.m_emissiveFactor = glm::make_vec3(material.emissiveFactor.data());
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
              << mat.m_emissiveFactor.b << std::endl;
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
        unsigned char *data = stbi_load(imagePath.c_str(), &width, &height, &components, 4 /* force 4 channels */);
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

void ProcessModel(const tinygltf::Model &model, std::vector<Model::Vertex> &vertices, std::vector<uint32_t> &indices,
                  std::vector<Model::Material> &materials, std::vector<Model::Texture> &textures,
                  std::vector<Model::SubMesh> &subMeshes)
{
    if (model.scenes.size() > 0)
    {
        const tinygltf::Scene &scene = model.scenes[model.defaultScene > -1 ? model.defaultScene : 0];

        for (int nodeIndex : scene.nodes)
        {
            ProcessNode(model, nodeIndex, glm::mat4(1.0f), vertices, indices, subMeshes);
        }
    }

    for (const auto &material : model.materials)
    {
        ProcessMaterial(material, materials);
    }

    for (const auto &image : model.images)
    {
        ProcessImage(image, "", textures);
    }
}

} // namespace

//----------------------------------------------------------------------
// Model Class Implementation

void Model::Load(const std::string &filename, const uint8_t *data, uint32_t size)
{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;
    bool result = false;

    if (data) {
        // Load from memory, binary file
        result = loader.LoadBinaryFromMemory(&model, &err, &warn, data, size);
    }
    else {
        // Load from file, either ASCII or binary
    
        const std::string basePath = filename.substr(0, filename.find_last_of("/"));
        std::string extension = filename.substr(filename.find_last_of(".") + 1);
        

        if (extension == "gltf") {
            result = loader.LoadASCIIFromFile(&model, &err, &warn, filename);
        } else if (extension == "glb") {
            result = loader.LoadBinaryFromFile(&model, &err, &warn, filename);
        } else {
            std::cerr << "Unsupported file format: " << extension << std::endl;
            return;
        }
    }

    // If succesful, process the model
    if (result)
    {
        ClearData();
        ProcessModel(model, m_vertices, m_indices, m_materials, m_textures, m_subMeshes);
        RecomputeBounds();
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

    m_transform = glm::rotate(glm::mat4(1.0f), -m_rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
}

void Model::ResetOrientation() noexcept
{
    m_rotationAngle = 0.0f;
}

const glm::mat4 &Model::GetTransform() const noexcept
{
    return m_transform;
}

void Model::GetBounds(glm::vec3 &minBounds, glm::vec3 &maxBounds) const noexcept
{
    minBounds = m_minBounds;
    maxBounds = m_maxBounds;
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

const Model::Texture *Model::GetTexture(int index) const noexcept
{
    if (index >= 0 && index < static_cast<int>(m_textures.size()))
    {
        return &m_textures[index];
    }
    return nullptr;
}

const std::vector<Model::SubMesh> &Model::GetSubMeshes() const noexcept
{
    return m_subMeshes;
}

void Model::ClearData()
{
    m_transform = glm::mat4(1.0f);
    m_rotationAngle = 0.0f;
    m_minBounds = glm::vec3(std::numeric_limits<float>::max());
    m_maxBounds = glm::vec3(std::numeric_limits<float>::lowest());
    m_vertices.clear();
    m_indices.clear();
    m_materials.clear();
    m_textures.clear();
    m_subMeshes.clear();
}

void Model::RecomputeBounds()
{
    m_minBounds = glm::vec3(std::numeric_limits<float>::max());
    m_maxBounds = glm::vec3(std::numeric_limits<float>::lowest());

    // Calculate the bounding box of the model
    for (const auto &vertex : m_vertices)
    {
        m_minBounds = glm::min(m_minBounds, vertex.m_position);
        m_maxBounds = glm::max(m_maxBounds, vertex.m_position);
    }
}