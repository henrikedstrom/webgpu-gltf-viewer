// Standard Library Headers
#include <iostream>
#include <string>
#include <filesystem>

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
// Internal Utility Functions

namespace
{
    void LoadTexture(const std::string &filename, Environment::Texture &texture)
    {
        // Load the texture
        int width, height, components;
        uint8_t *data = stbi_load(filename.c_str(), &width, &height, &components, 4 /* force 4 channels */);
        if (data)
        {
            components = 4; // force 4 channels
            texture.m_width = width;
            texture.m_height = height;
            texture.m_components = components;
            texture.m_data = std::vector<uint8_t>(data, data + (width * height * components));
            stbi_image_free(data);

            std::cout << "Loaded environment texture: " << filename << " (" << width << "x" << height << ")" << std::endl;
        }
        else
        {
            std::cerr << "Failed to load image: " << filename << std::endl;
        }
    }
} // namespace

//----------------------------------------------------------------------
// Environment Class Implementation

void Environment::Load(const std::string &filename)
{
    m_transform = glm::mat4(1.0f); // Reset the environment transformation matrix

    // Extract the base name, extension, and parent path
    std::filesystem::path filePath(filename);
    std::string baseName = filePath.stem().string();  // Extracts the filename without extension
    std::string extension = filePath.extension().string(); // Extracts the original extension
    std::string parentPath = filePath.has_parent_path() ? filePath.parent_path().string() + "/" : "";
    std::string irradianceFilename = parentPath + baseName + "_irradiance" + extension;

    // Load the background texture
    LoadTexture(filename, m_backgroundTexture);

    // Load the irradiance texture
    LoadTexture(irradianceFilename, m_irradianceTexture);
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

const Environment::Texture &Environment::GetBackgroundTexture() const noexcept
{
    return m_backgroundTexture;
}

const Environment::Texture &Environment::GetIrradianceTexture() const noexcept
{
    return m_irradianceTexture;
}