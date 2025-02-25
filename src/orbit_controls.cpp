// Third-Party Library Headers
#include <GLFW/glfw3.h>

// Project Headers
#include "camera.h"
#include "orbit_controls.h"

//----------------------------------------------------------------------
// OrbitControls Class Implementation

OrbitControls::OrbitControls(GLFWwindow *window, Camera *camera) : m_window(window), m_camera(camera)
{
    glfwSetWindowUserPointer(window, this);
    glfwSetCursorPosCallback(window, CursorPositionCallback);
    glfwSetScrollCallback(window, ScrollCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
}

void OrbitControls::CursorPositionCallback(GLFWwindow *window, double xpos, double ypos) noexcept
{
    auto controls = static_cast<OrbitControls *>(glfwGetWindowUserPointer(window));
    if (!controls)
    {
        return;
    }

    if (controls->m_mouseTumble || controls->m_mousePan)
    {
        glm::vec2 currentMouse = glm::vec2(xpos, ypos);
        glm::vec2 delta = currentMouse - controls->m_mouseLastPos;
        controls->m_mouseLastPos = currentMouse;
        int xrel = static_cast<int>(delta.x);
        int yrel = static_cast<int>(delta.y);
        if (controls->m_mouseTumble)
        {
            controls->m_camera->Tumble(xrel, yrel);
        }
        else if (controls->m_mousePan)
        {
            controls->m_camera->Pan(xrel, yrel);
        }
    }
}

void OrbitControls::ScrollCallback(GLFWwindow *window, double xoffset, double yoffset) noexcept
{
    auto controls = static_cast<OrbitControls *>(glfwGetWindowUserPointer(window));
    if (!controls)
    {
        return;
    }

    controls->m_camera->Zoom(0, static_cast<int>(yoffset * kZoomSensitivity));
}

void OrbitControls::MouseButtonCallback(GLFWwindow *window, int button, int action, int mods) noexcept
{
    auto controls = static_cast<OrbitControls *>(glfwGetWindowUserPointer(window));
    if (!controls)
    {
        return;
    }

    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    controls->m_mouseLastPos = glm::vec2(xpos, ypos);

    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {
        switch (action)
        {
        case GLFW_PRESS:
            if (mods & GLFW_MOD_SHIFT)
            {
                controls->m_mousePan = true;
            }
            else
            {
                controls->m_mouseTumble = true;
            }
            break;
        case GLFW_RELEASE:
            controls->m_mouseTumble = false;
            controls->m_mousePan = false;
            break;
        }
    }
    else if (button == GLFW_MOUSE_BUTTON_MIDDLE)
    {
        if (action == GLFW_PRESS)
        {
            controls->m_mousePan = true;
        }
        else if (action == GLFW_RELEASE)
        {
            controls->m_mousePan = false;
        }
    }
}
