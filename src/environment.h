#pragma once

// Standard Library Headers
#include <cstdint>
#include <string>
#include <vector>

// Third-Party Library Headers
#include <glm/glm.hpp>

// Environment Class
class Environment
{
  public:
    // Types
    struct Texture
    {
        std::string m_name;             // Name of the texture
        uint32_t m_width = 0;           // Width of the texture
        uint32_t m_height = 0;          // Height of the texture
        uint32_t m_components = 0;      // Components per pixel (e.g., 3 = RGB, 4 = RGBA)
        std::vector<uint8_t> m_data[6]; // Raw pixel data, stored per face
    };

    // Constructor
    Environment() = default;

    // Rule of 5
    Environment(const Environment &) = default;
    Environment &operator=(const Environment &) = default;
    Environment(Environment &&) = default;
    Environment &operator=(Environment &&) = default;

    // Public Interface
    void Load(const std::string &filename);
    void UpdateRotation(float rotationAngle);

    // Accessors
    const glm::mat4 &GetTransform() const noexcept;
    const Texture &GetBackgroundTexture() const noexcept;
    const Texture &GetIrradianceTexture() const noexcept;

  private:
    // Private Member Variables
    glm::mat4 m_transform{1.0f};
    Texture m_backgroundTexture;
    Texture m_irradianceTexture;
};