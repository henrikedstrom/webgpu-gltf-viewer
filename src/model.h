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

    // Constructor
    Model() = default;

    // Rule of 5
    Model(const Model &) = default;
    Model &operator=(const Model &) = default;
    Model(Model &&) = default;
    Model &operator=(Model &&) = default;

    // Public Interface
    void LoadModel(const std::string &filename);
    void Update(float deltaTime, bool animate);

    // Accessors
    const glm::mat4 &GetTransform() const noexcept;
    const std::vector<Vertex> &GetVertices() const noexcept;
    const std::vector<uint32_t> &GetIndices() const noexcept;

  private:
    // Private Member Variables
    glm::mat4 m_transform{1.0f};  // Model transformation matrix
    float m_rotationAngle = 0.0f; // Model rotation angle
    std::vector<Vertex> m_vertices;
    std::vector<uint32_t> m_indices;
};