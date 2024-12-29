#pragma once

#include <glm/glm.hpp>

class Camera
{
  public:
    void init(int width, int height);
    void tumble(int dx, int dy);
    void zoom(int dx, int dy);
    void pan(int dx, int dy);

    void setWidthAndHeight(int width, int height);

    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix() const;
    glm::vec3 getWorldPosition() const;

  private:
    int m_width;
    int m_height;

    float m_near;
    float m_far;

    glm::vec3 m_position;
    glm::vec3 m_target;

    glm::vec3 m_forward;
    glm::vec3 m_right;
    glm::vec3 m_up;
    glm::vec3 m_baseUp;
};
