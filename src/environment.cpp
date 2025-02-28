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

int FloorPow2(int x)
{
    int power = 1;
    while (power * 2 <= x)
        power *= 2;
    return power;
}

void LoadTexture(Environment::Texture &texture, int width, int height, const float *data)
{
    // Time the resampling of the environment texture
    auto start = std::chrono::high_resolution_clock::now();

    // Force 4 channels
    constexpr int components = 4;

    // Resample to cubemap faces
    const int cubemapSize = FloorPow2(height);
    texture.m_width = cubemapSize;
    texture.m_height = cubemapSize;
    texture.m_components = components;

    const glm::vec3 faceDirs[6] = {
        {1.0f, 0.0f, 0.0f},  // +X
        {-1.0f, 0.0f, 0.0f}, // -X
        {0.0f, 1.0f, 0.0f},  // +Y
        {0.0f, -1.0f, 0.0f}, // -Y
        {0.0f, 0.0f, 1.0f},  // +Z
        {0.0f, 0.0f, -1.0f}  // -Z
    };

    const glm::vec3 upVectors[6] = {
        {0.0f, -1.0f, 0.0f}, // +X
        {0.0f, -1.0f, 0.0f}, // -X
        {0.0f, 0.0f, 1.0f},  // +Y
        {0.0f, 0.0f, -1.0f}, // -Y
        {0.0f, -1.0f, 0.0f}, // +Z
        {0.0f, -1.0f, 0.0f}  // -Z
    };

    const glm::vec3 rightVectors[6] = {
        {0.0f, 0.0f, -1.0f}, // +X
        {0.0f, 0.0f, 1.0f},  // -X
        {1.0f, 0.0f, 0.0f},  // +Y
        {1.0f, 0.0f, 0.0f},  // -Y
        {1.0f, 0.0f, 0.0f},  // +Z
        {-1.0f, 0.0f, 0.0f}  // -Z
    };

    for (int face = 0; face < 6; ++face)
    {
        std::vector<Float16> &cubemapFace = texture.m_data[face];
        cubemapFace.resize(cubemapSize * cubemapSize * components);

        glm::vec3 faceDir = faceDirs[face];
        glm::vec3 up = upVectors[face];
        glm::vec3 right = rightVectors[face];

        for (int y = 0; y < cubemapSize; ++y)
        {
            for (int x = 0; x < cubemapSize; ++x)
            {
                // Compute the normalized direction vector for this cubemap face pixel
                float u = (x + 0.5f) / cubemapSize * 2.0f - 1.0f; // [-1, 1]
                float v = (y + 0.5f) / cubemapSize * 2.0f - 1.0f; // [-1, 1]
                glm::vec3 dir = glm::normalize(faceDir + u * right + v * up);

                // Convert direction to spherical coordinates
                float theta = acos(dir.y);       // Polar angle [0, π]
                float phi = atan2(dir.z, dir.x); // Azimuthal angle [-π, π]
                if (phi < 0.0f)
                {
                    phi += glm::two_pi<float>(); // Normalize to [0, 2π]
                }

                // Convert spherical coordinates to equirectangular UV
                float uEquirect = phi / glm::two_pi<float>();
                float vEquirect = theta / glm::pi<float>();

                // Convert UV to floating-point pixel coordinates
                float srcXF = uEquirect * (width - 1);
                float srcYF = vEquirect * (height - 1);

                // Calculate the integer and fractional parts
                int x0 = static_cast<int>(floor(srcXF));
                int x1 = (x0 + 1) % width; // Wrap horizontally
                int y0 = static_cast<int>(floor(srcYF));
                int y1 = std::min(y0 + 1, height - 1); // Clamp vertically

                float fx = srcXF - x0; // Fractional part in X
                float fy = srcYF - y0; // Fractional part in Y

                // Fetch the four nearest texels
                int index00 = (y0 * width + x0) * components; // Top-left
                int index10 = (y0 * width + x1) * components; // Top-right (wrapped horizontally)
                int index01 = (y1 * width + x0) * components; // Bottom-left
                int index11 = (y1 * width + x1) * components; // Bottom-right (wrapped horizontally)

                // Interpolate pixel values
                int dstIndex = (y * cubemapSize + x) * components;
                for (int c = 0; c < components; ++c)
                {
                    float value = (1 - fx) * (1 - fy) * data[index00 + c] + // Top-left
                                  fx * (1 - fy) * data[index10 + c] +       // Top-right
                                  (1 - fx) * fy * data[index01 + c] +       // Bottom-left
                                  fx * fy * data[index11 + c];              // Bottom-right

                    cubemapFace[dstIndex + c] = Float16(value);
                }
            }
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Resampled environment texture (" << width << "x" << height << " -> " << cubemapSize << "x"
              << cubemapSize << "x6) in " << elapsed.count() << " seconds" << std::endl;
}

template <typename LoaderFunc, typename... Args> 
void LoadFromSource(Environment::Texture &texture, LoaderFunc loader, Args &&...args)
{
    int width = 0;
    int height = 0;
    int channels = 0;

    // Call the provided loader (either stbi_loadf or stbi_loadf_from_memory)
    float *data = loader(std::forward<Args>(args)..., &width, &height, &channels, 4);

    if (!data)
    {
        std::cerr << "Failed to load image." << std::endl;
        std::cerr << "stb_image failure: " << stbi_failure_reason() << std::endl;
        return;
    }

    if (width != 2 * height)
    {
        std::cerr << "Error: Texture must have a 2:1 aspect ratio. Received: " << width << "x" << height << std::endl;
        stbi_image_free(data);
        return;
    }

    LoadTexture(texture, width, height, data);
    
    std::cout << "Loaded environment texture (" << width << "x" << height << ")" << std::endl;
    
    stbi_image_free(data);
}

} // namespace

//----------------------------------------------------------------------
// Environment Class Implementation

void Environment::Load(const std::string &filename, const uint8_t *data, uint32_t size)
{
    if (data) {
        LoadFromSource(m_texture, stbi_loadf_from_memory, data, size);
    }
    else {
        LoadFromSource(m_texture, stbi_loadf, filename.c_str());
    }

    m_transform = glm::mat4(1.0f);
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
