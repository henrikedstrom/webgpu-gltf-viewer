#pragma once

#include <glm/glm.hpp>

class Camera
{
  public:
    /// Constructs a camera with default parameters.
    Camera() = default;

    /// Constructs a camera with the specified viewport dimensions.
    Camera(int width, int height) : m_width(width), m_height(height)
    {
    }

    /// Defaulted copy constructor.
    Camera(const Camera &) = default;

    /// Defaulted copy assignment operator.
    Camera &operator=(const Camera &) = default;

    /// Defaulted move constructor.
    Camera(Camera &&) = default;

    /// Defaulted move assignment operator.
    Camera &operator=(Camera &&) = default;

    // Camera movement
    void Tumble(int dx, int dy);
    void Zoom(int dx, int dy);
    void Pan(int dx, int dy);

    // Resize the camera viewport
    void ResizeViewport(int width, int height);

    // Accessors
    glm::mat4 GetViewMatrix() const noexcept;
    glm::mat4 GetProjectionMatrix() const noexcept;
    glm::vec3 GetWorldPosition() const noexcept;

  private:
    // Screen dimensions
    int m_width{800};  // Default screen width
    int m_height{600}; // Default screen height

    // Clipping planes
    float m_near{0.1f};  // Near clipping plane
    float m_far{100.0f}; // Far clipping plane

    // Camera properties
    glm::vec3 m_position{0.0f, 0.0f, -3.0f}; // Default camera position
    glm::vec3 m_target{0.0f, 0.0f, 0.0f};    // Default target position

    // Basis vectors
    glm::vec3 m_forward{0.0f, 0.0f, 1.0f}; // Forward vector
    glm::vec3 m_right{1.0f, 0.0f, 0.0f};   // Right vector
    glm::vec3 m_up{0.0f, 1.0f, 0.0f};      // Up vector
    glm::vec3 m_baseUp{0.0f, 1.0f, 0.0f};  // Base up vector

    /// Updates the camera's basis vectors (forward, right, and up)
    void UpdateCameraVectors();
};
