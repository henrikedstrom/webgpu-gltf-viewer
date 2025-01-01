#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

class Model
{
  public:
    // Constructor
    Model() = default;

    // Rule of 5
    Model(const Model &) = default;
    Model &operator=(const Model &) = default;
    Model(Model &&) = default;
    Model &operator=(Model &&) = default;

    // Public Interface
    void LoadModel(const std::string &filename);
    void AnimateModel(float deltaTime);

    // Accessors
    const glm::mat4 &GetTransform() const noexcept;
    const std::vector<float> &GetVertices() const noexcept;
    const std::vector<uint32_t> &GetIndices() const noexcept;

  private:
    glm::mat4 m_transform{1.0f}; // Model transformation matrix
    float m_rotationAngle = 0.0f; // Model rotation angle
    std::vector<float> m_vertices;
    std::vector<uint32_t> m_indices;
};