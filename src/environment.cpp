// Standard Library Headers
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>

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

void DownsampleTexture(Environment::Texture &texture, int origWidth, int origHeight)
{
    std::cout << "Downsampling texture from " << origWidth << "x" << origHeight << " to 4096x2048." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    // Define target resolution (fixed 4096x2048; maintains 2:1 aspect ratio).
    const uint32_t newWidth = 4096;
    const uint32_t newHeight = 2048;
    std::vector<float> downsampled(newWidth * newHeight * 4, 0.0f);

    // Compute scale factors from destination to source.
    // Subtracting 1 ensures the last pixel maps correctly.
    float scaleX = float(origWidth - 1) / float(newWidth - 1);
    float scaleY = float(origHeight - 1) / float(newHeight - 1);

    // Bilinear downsampling loop.
    for (uint32_t j = 0; j < newHeight; ++j)
    {
        float origY = j * scaleY;
        int y0 = static_cast<int>(std::floor(origY));
        int y1 = std::min(y0 + 1, origHeight - 1);
        float dy = origY - y0;
        for (uint32_t i = 0; i < newWidth; ++i)
        {
            float origX = i * scaleX;
            int x0 = static_cast<int>(std::floor(origX));
            int x1 = std::min(x0 + 1, origWidth - 1);
            float dx = origX - x0;
            // Process each channel (RGBA).
            for (int c = 0; c < 4; ++c)
            {
                float c00 = texture.m_data[(y0 * origWidth + x0) * 4 + c];
                float c10 = texture.m_data[(y0 * origWidth + x1) * 4 + c];
                float c01 = texture.m_data[(y1 * origWidth + x0) * 4 + c];
                float c11 = texture.m_data[(y1 * origWidth + x1) * 4 + c];
                // Bilinear interpolation: horizontal then vertical.
                float top = c00 + dx * (c10 - c00);
                float bottom = c01 + dx * (c11 - c01);
                float value = top + dy * (bottom - top);
                downsampled[(j * newWidth + i) * 4 + c] = value;
            }
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Downsampling took " << elapsed.count() << " seconds." << std::endl;

    // Update the texture with downsampled data.
    texture.m_width = newWidth;
    texture.m_height = newHeight;
    texture.m_data = std::move(downsampled);
}

template <typename LoaderFunc, typename... Args>
bool LoadFromSource(Environment::Texture &texture, LoaderFunc loader, Args &&...args)
{
    auto t0 = std::chrono::high_resolution_clock::now();

    int width = 0;
    int height = 0;
    int channels = 0;

    // Call the provided loader (either stbi_loadf or stbi_loadf_from_memory)
    float *data = loader(std::forward<Args>(args)..., &width, &height, &channels, 4);

    if (!data)
    {
        std::cerr << "Failed to load image." << std::endl;
        std::cerr << "stb_image failure: " << stbi_failure_reason() << std::endl;
        return false;
    }

    if (width != 2 * height)
    {
        std::cerr << "Error: Texture must have a 2:1 aspect ratio. Received: " << width << "x" << height << std::endl;
        stbi_image_free(data);
        return false;
    }

    // Loading successful, populate the texture struct.
    texture.m_width = width;
    texture.m_height = height;
    texture.m_components = 4; // Assuming RGBA
    texture.m_data.resize(width * height * 4);
    std::copy(data, data + (width * height * 4), texture.m_data.begin());

    auto t1 = std::chrono::high_resolution_clock::now();
    double durationMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "Loaded environment texture (" << width << "x" << height << ")" << 
    " in " << durationMs << "ms" << std::endl;

    stbi_image_free(data);

    // If the texture is larger than 4096x2048, downsample it to that resolution to make it more portable.
    if (width > 4096)
    {
        DownsampleTexture(texture, width, height);
    }

    return true;
}

} // namespace

//----------------------------------------------------------------------
// Environment Class Implementation

bool Environment::Load(const std::string &filename, const uint8_t *data, uint32_t size)
{
    bool success = false;

    if (data) {
        success = LoadFromSource(m_texture, stbi_loadf_from_memory, data, size);
    }
    else {
        success = LoadFromSource(m_texture, stbi_loadf, filename.c_str());
    }

    if (success) {
        m_texture.m_name = filename;
        m_transform = glm::mat4(1.0f);
    }

    return success;
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
