// Third-Party Library Headers
#include <glm/gtc/matrix_transform.hpp>

// Project Headersq
#include "camera.h"

//----------------------------------------------------------------------
// Internal Constants

namespace
{
constexpr float kTumbleSpeed = 0.004f;
constexpr float kTiltClamp = 0.98f; // Restricts the forward vector's vertical component to avoid gimbal lock.
constexpr float kPanSpeed = 0.01f;
constexpr float kZoomSpeed = 0.01f;
constexpr float kDefaultFOV = 45.0f;
} // namespace

//----------------------------------------------------------------------
// Camera Class Implementation

void Camera::Tumble(int dx, int dy)
{
    // Rotate around world Y-axis (up-axis)
    {
        // Calculate the offset from the camera to the target
        glm::vec3 cameraOffset = m_position - m_target;

        // Rotate the camera offset around the world Y-axis
        float degrees = float(dx) * kTumbleSpeed;
        float newX = cameraOffset[0] * cos(degrees) - cameraOffset[2] * sin(degrees);
        float newZ = cameraOffset[0] * sin(degrees) + cameraOffset[2] * cos(degrees);

        // Update the camera position and orientation
        cameraOffset[0] = newX;
        cameraOffset[2] = newZ;
        m_position = m_target + cameraOffset;

        // Recalculate the camera's basis vectors
        UpdateCameraVectors();
    }

    // Tilt around local X-axis (right-axis)
    {
        glm::vec3 originalPosition = m_position;
        glm::vec3 originalForward = m_forward;

        // Calculate the offset from the camera to the target
        glm::vec3 cameraOffset = m_position - m_target;
        float degrees = float(dy) * kTumbleSpeed;

        // Decompose the offset into the camera's local axes
        float rightComponent = glm::dot(cameraOffset, m_right);
        float upComponent = glm::dot(cameraOffset, m_up);
        float forwardComponent = glm::dot(cameraOffset, m_forward);

        // Rotate the offset around the local X-axis (right-axis)
        float newUp = upComponent * cos(degrees) - forwardComponent * sin(degrees);
        float newForward = upComponent * sin(degrees) + forwardComponent * cos(degrees);

        // Reconstruct the new camera position
        cameraOffset = (m_right * rightComponent) + (m_up * newUp) + (m_forward * newForward);
        m_position = m_target + cameraOffset;

        // Clamp the forward vector to prevent gimbal lock
        m_forward = glm::normalize(m_target - m_position);
        if (std::abs(m_forward[1]) > kTiltClamp)
        {
            m_position = originalPosition;
            m_forward = originalForward;
        }

        // Recalculate the camera's basis vectors
        UpdateCameraVectors();
    }
}

void Camera::Zoom(int dx, int dy)
{
    const float delta = (-dx + dy) * kZoomSpeed;

    // Move the camera along the forward vector
    m_position += m_forward * delta;
}

void Camera::Pan(int dx, int dy)
{
    const float delta_x = -dx * kPanSpeed;
    const float delta_y = dy * kPanSpeed;

    // Move the camera along the right and up vectors
    m_position += m_up * delta_y + m_right * delta_x;
    m_target += m_up * delta_y + m_right * delta_x;
}

void Camera::ResizeViewport(int width, int height)
{
    if (width > 0 && height > 0)
    {
        m_width = width;
        m_height = height;
    }
}

glm::mat4 Camera::GetViewMatrix() const noexcept
{
    return glm::lookAt(m_position, m_target, m_up);
}

glm::mat4 Camera::GetProjectionMatrix() const noexcept
{
    const float ratio = static_cast<float>(m_width) / m_height;
    return glm::perspective(glm::radians(kDefaultFOV), ratio, m_near, m_far);
}

glm::vec3 Camera::GetWorldPosition() const noexcept
{
    return m_position;
}

void Camera::UpdateCameraVectors()
{
    m_forward = glm::normalize(m_target - m_position);
    m_right = glm::normalize(glm::cross(m_forward, m_baseUp));
    m_up = glm::normalize(glm::cross(m_right, m_forward));
}