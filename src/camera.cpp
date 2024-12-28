#include "camera.h"

#include <glm/gtc/matrix_transform.hpp>

void Camera::init(int width, int height)
{
  m_width = width;
  m_height = height;

  m_near = 0.1f; // arbitrary for now...
  m_far = 100.0f; // arbitrary for now...

  m_position = glm::vec3(0.0f, 0.0f, -3.0f);
  m_target = glm::vec3(0.0f, 0.0f, 0.0f);

  m_forward = glm::vec3(0.0f, 0.0f, 1.0f);
  m_right = glm::vec3(1.0f, 0.0f, 0.0f);
  m_up = glm::vec3(0.0f, 1.0f, 0.0f);
  m_baseUp = glm::vec3(0.0f, 1.0f, 0.0f);
}


void Camera::setWidthAndHeight(int width, int height)
{
  m_width = width;
  m_height = height;
}

void Camera::tumble(int dx, int dy)
{
  // rotate around world y-axis (up-axis)
  {
    glm::vec3 tmp = m_position - m_target;
    float degrees = float(dx) * 0.004f;

    float old_x = tmp[0];
    float old_y = tmp[1];
    float old_z = tmp[2];

    float new_x = old_x * cos(degrees) - old_z * sin(degrees);
    float new_z = old_x * sin(degrees) + old_z * cos(degrees);

    tmp[0] = new_x;
    tmp[1] = old_y;
    tmp[2] = new_z;

    m_position = m_target + tmp;
    m_forward = m_target - m_position;
    m_forward = glm::normalize(m_forward);
    m_right = glm::cross(m_forward, m_baseUp);
    m_up = glm::cross(m_right, m_forward);
    m_up = glm::normalize(m_up);
    m_right = glm::cross(m_forward, m_up);
    m_right = glm::normalize(m_right);
  }

  // tilt around local x-axis (right-axis)
  {
    glm::vec3 orig_pos = m_position;
    glm::vec3 orig_forward = m_forward;

    glm::vec3 tmp = m_position - m_target;
    float degrees = float(dy) * 0.004f;

    float old_x = glm::dot(tmp, m_right);
    float old_y = glm::dot(tmp, m_up);
    float old_z = glm::dot(tmp, m_forward);

    float new_y = old_y * cos(degrees) - old_z * sin(degrees);
    float new_z = old_y * sin(degrees) + old_z * cos(degrees);

    tmp = (m_up * new_y) + (m_right * old_x) + (m_forward * new_z);

    m_position = m_target + tmp;
    m_forward = m_target - m_position;
    m_forward = glm::normalize(m_forward);

    // clamp forward vector
    static const float maxVerticalComponent = 0.9995f;
    if (m_forward[1] > maxVerticalComponent)
    {
      m_forward = orig_forward;
      m_position = orig_pos;
    }
    if (m_forward[1] < -maxVerticalComponent)
    {
      m_forward = orig_forward;
      m_position = orig_pos;
    }

    m_up = glm::cross(m_right, m_forward);
    m_up = glm::normalize(m_up);
    m_right = glm::cross(m_forward, m_baseUp);
    m_right = glm::normalize(m_right);

    m_up = glm::cross(m_right, m_forward);
    m_up = glm::normalize(m_up);
  }
}


void Camera::zoom(int dx, int dy)
{
  float sizeFactor = 1.0f;
  const float speed = 0.01f;
  //float distanceToFocus = length(m_position - m_target);
  float delta = (-dx + dy) * (speed * sizeFactor);

  // Move the camera
  m_position += m_forward * delta;
  //m_target += m_forward * delta;

  // Scale size factor by the same factor we just scaled distance to focus
  // sizeFactor *= 1.0f - (delta / distanceToFocus);
}


void Camera::pan(int dx, int dy)
{
  float sizeFactor = 1.0f;
  const float speed = 0.01f;

  m_position += m_up * (dy * (speed * sizeFactor));
  //m_target += m_up * (dy * (speed * sizeFactor));

  m_position += m_right * (-dx * (speed * sizeFactor));
  //m_target += m_right * (-dx * (speed * sizeFactor));
}


glm::mat4 Camera::getViewMatrix() const
{
  return glm::lookAt(m_position, m_target, m_up);
}


glm::mat4 Camera::getProjectionMatrix() const
{
  const float ratio = static_cast<float>(m_width) / static_cast<float>(m_height);
  return glm::perspective(glm::radians(45.0f), ratio, m_near, m_far);
}


glm::vec3 Camera::getWorldPosition() const
{
  return m_position;
}