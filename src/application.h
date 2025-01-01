#pragma once

#include <cstdint>
#include <memory>

#include "camera.h"
#include "model.h"
#include "orbit_controls.h"
#include "renderer.h"

class GLFWwindow;

class Application
{
  public:

    // Static Instance Getter
    static Application *GetInstance() { return s_instance; }

    // Constructor and Destructor
    explicit Application(uint32_t width, uint32_t height);
    ~Application();

    // Deleted Functions
    Application(const Application &) = delete;
    Application &operator=(const Application &) = delete;
    Application(Application &&) = delete;
    Application &operator=(Application &&) = delete;

    // Public Interface
    void Run();
    void OnKeyPressed(int key);

  private:

    // Private Member Functions
    void MainLoop();
    void ProcessFrame();

    // Static Instance
    static Application *s_instance;

    uint32_t m_width;
    uint32_t m_height;
    bool m_quitApp = false;
    bool m_animateModel = true;

    GLFWwindow *m_window = nullptr;
    Camera m_camera;
    Model m_model;
    Renderer m_renderer;

    std::unique_ptr<OrbitControls> m_controls;
};