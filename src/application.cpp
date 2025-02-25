
// Third-Party Library Headers
#include <GLFW/glfw3.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

// Project Headers
#include "application.h"

// Static Application Instance
Application *Application::s_instance = nullptr;

//----------------------------------------------------------------------
// Internal Utility Functions

namespace
{

void KeyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    static bool keyState[GLFW_KEY_LAST] = {false};

    if (key >= 0 && key < GLFW_KEY_LAST)
    {
        bool keyPressed = action == GLFW_PRESS && !keyState[key];
        bool keyReleased = action == GLFW_RELEASE && keyState[key];

        if (action == GLFW_PRESS)
        {
            keyState[key] = true;
        }
        else if (action == GLFW_RELEASE)
        {
            keyState[key] = false;
        }

        if (keyPressed)
        {
            Application::GetInstance()->OnKeyPressed(key);
        }
    }
}

void RepositionCamera(Camera &camera, const Model &model)
{
    glm::vec3 minBounds, maxBounds;
    model.GetBounds(minBounds, maxBounds);

    glm::vec3 center = (minBounds + maxBounds) * 0.5f;
    float radius = glm::length(maxBounds - minBounds) * 0.5f;
    float distance = radius / sin(glm::radians(camera.GetFOV() * 0.5f));

    // Calculate the camera position
    glm::vec3 position = center + glm::vec3(0.0f, 0.0f, distance);

    // Update the camera
    camera.SetPosition(position);
    camera.SetTarget(center);
    camera.SetNearFarPlanes(radius * 0.01f, distance + radius * 100.0f);
}

} // namespace

//----------------------------------------------------------------------
// Application Class Implementation

Application *Application::GetInstance()
{
    return s_instance;
}

Application::Application(uint32_t width, uint32_t height) : m_width(width), m_height(height)
{
    assert(!s_instance); // Ensure only one instance exists
    s_instance = this;
}

Application::~Application()
{
    if (m_window)
    {
        glfwDestroyWindow(m_window);
    }
    glfwTerminate();
    s_instance = nullptr;
}

void Application::Run()
{
    if (!glfwInit())
    {
        return;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_window = glfwCreateWindow(m_width, m_height, "WebGPU window", nullptr, nullptr);

    m_camera.ResizeViewport(m_width, m_height);

    // Setup input callbacks
    m_controls = std::make_unique<OrbitControls>(m_window, &m_camera);
    glfwSetKeyCallback(m_window, KeyCallback);
    glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow *window, int width, int height) {
        Application::GetInstance()->OnResize(width, height);
    });
    glfwSetDropCallback(m_window, [](GLFWwindow *window, int count, const char **paths) {
        Application::GetInstance()->OnFileDropped(count, paths);
    });

    m_environment.Load("./assets/environments/helipad.hdr");

    m_model.Load("./assets/models/DamagedHelmet/DamagedHelmet.gltf");
    // m_model.Load("./assets/models/SciFiHelmet/SciFiHelmet.gltf");
    RepositionCamera(m_camera, m_model);

    m_renderer.Initialize(m_window, &m_camera, &m_environment, m_model, m_width, m_height, [this]() { MainLoop(); });
}

void Application::MainLoop()
{
#if defined(__EMSCRIPTEN__)
    // Pass a pointer to ProcessFrame via the Emscripten main loop
    emscripten_set_main_loop_arg([](void *arg) { static_cast<Application *>(arg)->ProcessFrame(); }, this, 0, false);
#else
    while (!glfwWindowShouldClose(m_window) && !m_quitApp)
    {
        glfwPollEvents();

        ProcessFrame();
    }
#endif
}

void Application::ProcessFrame()
{
    // Animate the model (if enabled)
    m_model.Update(0.01f, m_animateModel);

    // Render a frame
    m_renderer.Render(m_model.GetTransform());
}

void Application::OnKeyPressed(int key)
{
    if (key == GLFW_KEY_A)
    {
        m_animateModel = !m_animateModel;
    }
    else if (key == GLFW_KEY_ESCAPE)
    {
        m_quitApp = true;
    }
    else if (key == GLFW_KEY_R)
    {
        m_renderer.ReloadShaders();
    }
    else if (key == GLFW_KEY_HOME)
    {
        RepositionCamera(m_camera, m_model);
    }
}

void Application::OnResize(int width, int height)
{
    m_width = width;
    m_height = height;
    m_camera.ResizeViewport(width, height);
    m_renderer.Resize(width, height);
}

void Application::OnFileDropped(int count, const char **paths)
{
    if (count > 0)
    {
        std::string filepath = paths[0]; // Load only the first file
        std::cout << "Loading model: " << filepath << std::endl;
        m_model.Load(filepath);
        RepositionCamera(m_camera, m_model);
        m_renderer.UpdateModel(m_model);
    }
}
