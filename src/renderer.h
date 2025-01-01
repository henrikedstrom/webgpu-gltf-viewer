#pragma once

#include <functional>
#include <GLFW/glfw3.h>

class Camera;
class Model;

class Renderer
{
  public:
    Renderer() = default;

    void Initialize(GLFWwindow *window, Camera *camera, Model *model, uint32_t width, uint32_t height,
                    const std::function<void()> &callback);
    void Render() const;

  private:
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    GLFWwindow *m_window = nullptr;
    Camera *m_camera = nullptr;
    Model *m_model = nullptr;
};
