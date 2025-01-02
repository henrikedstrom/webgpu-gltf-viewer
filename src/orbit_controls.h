#pragma once

// Third-Party Library Headers
#include <glm/glm.hpp>

// Forward Declarations
class Camera;
struct GLFWwindow;

// OrbitControls Class
class OrbitControls
{
  public:
    // Constructor
    explicit OrbitControls(GLFWwindow *window, Camera *camera);

    // Rule of 5
    OrbitControls(const OrbitControls &) = delete;
    OrbitControls &operator=(const OrbitControls &) = delete;
    OrbitControls(OrbitControls &&) = default;
    OrbitControls &operator=(OrbitControls &&) = default;

  private:
    // Static Callback Functions
    static void CursorPositionCallback(GLFWwindow *window, double xpos, double ypos) noexcept;
    static void ScrollCallback(GLFWwindow *window, double xoffset, double yoffset) noexcept;
    static void MouseButtonCallback(GLFWwindow *window, int button, int action, int mods) noexcept;

    // Static Constants
    static constexpr float kZoomSensitivity = 30.0f;

    // Private Member Variables
    GLFWwindow *m_window; // Non-owning pointer
    Camera *m_camera;     // Non-owning pointer
    bool m_mouseTumble{false};
    bool m_mousePan{false};
    glm::vec2 m_mouseLastPos{0};
};
