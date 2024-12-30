#pragma once

#include <glm/glm.hpp>

class Camera;
class GLFWwindow;

class OrbitControls
{
  public:
    explicit OrbitControls(GLFWwindow *window, Camera *camera);

    OrbitControls(const OrbitControls &) = delete;
    OrbitControls &operator=(const OrbitControls &) = delete;

    OrbitControls(OrbitControls &&) = default;
    OrbitControls &operator=(OrbitControls &&) = default;

  private:
    static constexpr float kZoomSensitivity = 30.0f;

    GLFWwindow *m_window; // Non-owning pointer
    Camera *m_camera;     // Non-owning pointer
    bool m_mouseTumble{false};
    bool m_mousePan{false};
    glm::vec2 m_mouseLastPos{0};

    static void CursorPositionCallback(GLFWwindow *window, double xpos, double ypos) noexcept;
    static void ScrollCallback(GLFWwindow *window, double xoffset, double yoffset) noexcept;
    static void MouseButtonCallback(GLFWwindow *window, int button, int action, int mods) noexcept;
};
