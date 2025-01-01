
// Third-Party Library Headers
#include <GLFW/glfw3.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#include "application.h"

Application *Application::s_instance = nullptr;

//----------------------------------------------------------------------
// Input Callbacks

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

    // Setup mouse and keyboard input callbacks
    m_controls = std::make_unique<OrbitControls>(m_window, &m_camera);
    glfwSetKeyCallback(m_window, KeyCallback);

    m_renderer.Initialize(m_window, &m_camera, m_width, m_height, [this]() { MainLoop(); });
}

void Application::MainLoop()
{
#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop_arg([](void *arg) { static_cast<Application *>(arg)->m_renderer.RenderFrame(); }, this, 0,
                                 false);
#else
    while (!glfwWindowShouldClose(m_window) && !m_quitApp)
    {
        glfwPollEvents();
        m_renderer.RenderFrame();
    }
#endif
}

void Application::OnKeyPressed(int key)
{
    static bool isAnimating = true;

    if (key == GLFW_KEY_A)
    {
        isAnimating = !isAnimating; // Toggle the animation state
        m_renderer.SetAnimating(isAnimating);
    }
    else if (key == GLFW_KEY_ESCAPE)
    {
        m_quitApp = true;
    }
}