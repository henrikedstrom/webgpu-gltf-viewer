#pragma once

// Third-Party Library Headers
#include <glm/glm.hpp>

// Camera Class
class Camera
{
  public:
    // Constructors
    Camera() = default;
    Camera(int width, int height) : m_width(width), m_height(height)
    {
    }

    // Rule of 5
    Camera(const Camera &) = default;
    Camera &operator=(const Camera &) = default;
    Camera(Camera &&) = default;
    Camera &operator=(Camera &&) = default;

    // Public Interface
    void Tumble(int dx, int dy);
    void Zoom(int dx, int dy);
    void Pan(int dx, int dy);
    void SetPosition(const glm::vec3 &position);
    void SetTarget(const glm::vec3 &target);
    void SetNearFarPlanes(float near, float far);
    void ResizeViewport(int width, int height);

    // Accessors
    glm::mat4 GetViewMatrix() const noexcept;
    glm::mat4 GetProjectionMatrix() const noexcept;
    glm::vec3 GetWorldPosition() const noexcept;
    float GetFOV() const noexcept;

  private:
    // Updates the camera's basis vectors (forward, right, and up)
    void UpdateCameraVectors();

    // Screen dimensions
    int m_width{800};  // Default screen width
    int m_height{600}; // Default screen height

    // Clipping planes
    float m_near{0.1f};  // Near clipping plane
    float m_far{100.0f}; // Far clipping plane

    // Camera properties
    glm::vec3 m_position{0.0f, 0.0f, 5.0f}; // Default camera position
    glm::vec3 m_target{0.0f, 0.0f, 0.0f};   // Default target position

    // Basis vectors
    glm::vec3 m_forward{0.0f, 0.0f, 1.0f}; // Forward vector
    glm::vec3 m_right{1.0f, 0.0f, 0.0f};   // Right vector
    glm::vec3 m_up{0.0f, 1.0f, 0.0f};      // Up vector
    glm::vec3 m_baseUp{0.0f, 1.0f, 0.0f};  // Base up vector
};
