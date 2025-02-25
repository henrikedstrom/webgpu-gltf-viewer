#pragma once

// Standard Library Headers
#include <cstdint>
#include <memory>

// Project Headers
#include "camera.h"
#include "environment.h"
#include "model.h"
#include "orbit_controls.h"
#include "renderer.h"

// Forward Declarations
struct GLFWwindow;

// Application Class
class Application
{
  public:
    // Static Instance Getter
    static Application *GetInstance();

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
    void OnResize(int width, int height);
    void OnFileDropped(int count, const char **paths);

  private:
    // Private Member Functions
    void MainLoop();
    void ProcessFrame();

    // Static Instance
    static Application *s_instance;

    // Private Member Variables
    uint32_t m_width;
    uint32_t m_height;
    bool m_quitApp = false;
    bool m_animateModel = true;

    GLFWwindow *m_window = nullptr;
    Camera m_camera;
    Environment m_environment;
    Model m_model;
    Renderer m_renderer;

    std::unique_ptr<OrbitControls> m_controls;
};