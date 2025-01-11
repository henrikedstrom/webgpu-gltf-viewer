// Standard Library Headers
#include <iostream>

// Third-Party Library Headers
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RIGHT_HANDED
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <glm/gtx/polar_coordinates.hpp>

#include <stb_image.h>

// Project Headers
#include "environment.h"

//----------------------------------------------------------------------
// Environment Class Implementation

void Environment::Load(const std::string &filename)
{
    m_transform = glm::mat4(1.0f); // Reset the environment transformation matrix
    m_texture.m_name = filename;   // Set the texture name

    // Load the environment texture
    int width, height, components;
    uint8_t *data = stbi_load(filename.c_str(), &width, &height, &components, 4 /* force 4 channels */);
    if (data)
    {
        components = 4; // force 4 channels
        m_texture.m_width = width;
        m_texture.m_height = height;
        m_texture.m_components = components;
        m_texture.m_data = std::vector<uint8_t>(data, data + (width * height * components));
        stbi_image_free(data);

        std::cout << "Loaded environment texture: " << filename << " (" << width << "x" << height << ")" << std::endl;
    }
    else
    {
        std::cerr << "Failed to load image: " << filename << std::endl;
    }
}

void Environment::UpdateRotation(float rotationAngle)
{
    // Rotate the environment around the Y-axis
    m_transform = glm::rotate(glm::mat4(1.0f), rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
}

const glm::mat4 &Environment::GetTransform() const noexcept
{
    return m_transform;
}

const Environment::Texture &Environment::GetTexture() const noexcept
{
    return m_texture;
}
