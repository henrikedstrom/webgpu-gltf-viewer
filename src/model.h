#pragma once

// Standard Library Headers
#include <cstdint>
#include <string>
#include <vector>

// Third-Party Library Headers
#include <glm/glm.hpp>

// Model Class
class Model
{
  public:
    // Types
    struct Vertex
    {
        glm::vec3 m_position;  // POSITION (vec3)
        glm::vec3 m_normal;    // NORMAL (vec3)
        glm::vec4 m_tangent;   // TANGENT (vec4)
        glm::vec2 m_texCoord0; // TEXCOORD_0 (vec2)
        glm::vec2 m_texCoord1; // TEXCOORD_1 (vec2)
        glm::vec4 m_color;     // COLOR_0 (vec4)
    };

    struct Material
    {
        glm::vec4 m_baseColorFactor = glm::vec4(1.0f); // Base color factor
        glm::vec4 m_emissiveFactor = glm::vec4(0.0f);  // Emissive color factor
        float m_metallicFactor = 1.0f;                 // Metallic factor
        float m_roughnessFactor = 1.0f;                // Roughness factor
        int m_baseColorTexture = -1;                   // Index of base color texture
        int m_metallicRoughnessTexture = -1;           // Index of metallic-roughness texture
        int m_normalTexture = -1;                      // Index of normal texture
        int m_emissiveTexture = -1;                    // Index of emissive texture
        int m_occlusionTexture = -1;                   // Index of occlusion texture
    };

    struct Texture
    {
        std::string m_name;          // Name of the texture
        uint32_t m_width = 0;        // Width of the texture
        uint32_t m_height = 0;       // Height of the texture
        uint32_t m_components = 0;   // Components per pixel (e.g., 3 = RGB, 4 = RGBA)
        std::vector<uint8_t> m_data; // Raw pixel data
    };

    // Constructor
    Model() = default;

    // Rule of 5
    Model(const Model &) = default;
    Model &operator=(const Model &) = default;
    Model(Model &&) = default;
    Model &operator=(Model &&) = default;

    // Public Interface
    void Load(const std::string &filename, const uint8_t *data = 0, uint32_t size = 0);
    void Update(float deltaTime, bool animate);

    // Accessors
    const glm::mat4 &GetTransform() const noexcept;
    void GetBounds(glm::vec3 &minBounds, glm::vec3 &maxBounds) const noexcept;
    const std::vector<Vertex> &GetVertices() const noexcept;
    const std::vector<uint32_t> &GetIndices() const noexcept;
    const std::vector<Material> &GetMaterials() const noexcept;
    const std::vector<Texture> &GetTextures() const noexcept;
    const Texture *GetTexture(int index) const noexcept;

  private:
    // Private Member Functions
    void ClearData();
    void RecomputeBounds();

    // Private Member Variables
    glm::mat4 m_transform{1.0f};  // Model transformation matrix
    float m_rotationAngle = 0.0f; // Model rotation angle
    glm::vec3 m_minBounds;        // Minimum bounds of the model
    glm::vec3 m_maxBounds;        // Maximum bounds of the model
    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;
    std::vector<Material> m_materials;
    std::vector<Texture> m_textures;
};