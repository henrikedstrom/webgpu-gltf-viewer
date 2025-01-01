#pragma once

#include <functional>
#include <GLFW/glfw3.h>

#include "camera.h"

class Renderer
{
  public:
    Renderer() = default;

    void Initialize(GLFWwindow *window, Camera *camera, uint32_t width, uint32_t height,
                    const std::function<void()> &callback);
    void RenderFrame();

    // Temp
    void SetAnimating(bool animating) { m_isAnimating = animating; }
    
  private:
    GLFWwindow *m_window;
    uint32_t m_width;
    uint32_t m_height;
    Camera *m_camera;

    bool m_isAnimating = true;
};
