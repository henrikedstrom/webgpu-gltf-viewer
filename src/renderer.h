#pragma once

// Standard Library Headers
#include <cstdint>
#include <functional>

// Third-Party Library Headers
#include <GLFW/glfw3.h>

// Forward Declarations
class Camera;
class Model;

// Renderer Class
class Renderer
{
  public:
    // Constructor
    Renderer() = default;

    // Rule of 5
    Renderer(const Renderer &) = default;
    Renderer &operator=(const Renderer &) = default;
    Renderer(Renderer &&) = default;
    Renderer &operator=(Renderer &&) = default;

    // Public Interface
    void Initialize(GLFWwindow *window, Camera *camera, Model *model, uint32_t width, uint32_t height,
                    const std::function<void()> &callback);
    void Render() const;

  private:
    // Private Member Variables
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    GLFWwindow *m_window = nullptr;
    Camera *m_camera = nullptr;
    Model *m_model = nullptr;
};
