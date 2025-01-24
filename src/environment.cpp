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

void LoadTexture(const std::string &filename, Environment::Texture &texture)
{
    auto start = std::chrono::high_resolution_clock::now();

    // Load the texture
    int width, height, components;
    float *data = stbi_loadf(filename.c_str(), &width, &height, &components, 4 /* force 4 channels */);
    if (!data)
    {
        std::cerr << "Failed to load image: " << filename << std::endl;
        return;
    }
    if (width != 2 * height) {
        std::cerr << "Error: Texture must have a 2:1 aspect ratio. Received: " << width << "x" << height << std::endl;
        stbi_image_free(data);
        return;
    }

    std::cout << "Loaded environment texture: " << filename << " (" << width << "x" << height << ")" << std::endl;

    // Force 4 channels
    components = 4;

    // Resample to cubemap faces
    const int cubemapSize = height; // Size of each cubemap face
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
                if (phi < 0.0f) {
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

    // Free the image data
    stbi_image_free(data);


    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Resampled environment texture (" << width << "x" << height << " -> " << cubemapSize << "x"
              << cubemapSize << "x6) in " << elapsed.count() << " seconds" << std::endl;
}

inline float RadicalInverse_VdC(unsigned int bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f; // Divide by 2^32
}

inline glm::vec2 Hammersley(uint32_t i, uint32_t N)
{
    return glm::vec2(float(i) / float(N), RadicalInverse_VdC(i));
}

glm::vec3 ImportanceSampleHemisphere(uint32_t sampleIndex, uint32_t sampleCount, const glm::vec3 &normal)
{
    glm::vec2 xi = Hammersley(sampleIndex, sampleCount);

    float phi = 2.0f * glm::pi<float>() * xi.x;
    float cosTheta = sqrt(1.0f - xi.y);
    float sinTheta = sqrt(xi.y);

    // Convert spherical coordinates to Cartesian
    glm::vec3 tangent = glm::normalize(glm::cross(normal, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 bitangent = glm::cross(normal, tangent);
    glm::vec3 sampleDir = cosTheta * normal + sinTheta * (cos(phi) * tangent + sin(phi) * bitangent);

    return glm::normalize(sampleDir);
}

glm::vec3 SampleEnvironment(const glm::vec3 &dir, const Environment::Texture &environmentTexture)
{
    glm::vec3 absDir = glm::abs(dir);
    int face;
    glm::vec2 uv;

    // Determine the target cube map face and UV coordinates
    if (absDir.x >= absDir.y && absDir.x >= absDir.z)
    {
        face = (dir.x > 0.0f) ? 0 : 1; // +X or -X
        uv = glm::vec2(dir.z, -dir.y) / absDir.x;
    }
    else if (absDir.y >= absDir.x && absDir.y >= absDir.z)
    {
        face = (dir.y > 0.0f) ? 2 : 3; // +Y or -Y
        uv = glm::vec2(dir.x, dir.z) / absDir.y;
    }
    else
    {
        face = (dir.z > 0.0f) ? 4 : 5; // +Z or -Z
        uv = glm::vec2(-dir.x, -dir.y) / absDir.z;
    }

    // Convert from [-1, 1] to [0, 1]
    uv = uv * 0.5f + 0.5f;

    // Map UV to pixel coordinates
    int x = glm::clamp(int(uv.x * (environmentTexture.m_width - 1)), 0, int(environmentTexture.m_width - 1));
    int y = glm::clamp(int(uv.y * (environmentTexture.m_height - 1)), 0, int(environmentTexture.m_height - 1));
    int index = (y * environmentTexture.m_width + x) * environmentTexture.m_components;

    assert(face >= 0 && face < 6);
    assert(index >= 0 && index < environmentTexture.m_data[face].size());

    // Sample the color from the correct face
    glm::vec3 color(
        environmentTexture.m_data[face][index],
        environmentTexture.m_data[face][index + 1],
        environmentTexture.m_data[face][index + 2]);

    return color;
}


void GenerateIrradianceTexture(const Environment::Texture &environmentTexture, Environment::Texture &irradianceTexture)
{
    auto start = std::chrono::high_resolution_clock::now();

    const int cubemapSize = 64;
    irradianceTexture.m_width = cubemapSize;
    irradianceTexture.m_height = cubemapSize;
    irradianceTexture.m_components = environmentTexture.m_components;

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
        std::vector<Float16> &cubemapFace = irradianceTexture.m_data[face];
        cubemapFace.resize(cubemapSize * cubemapSize * environmentTexture.m_components);

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

                // Integrate incoming radiance from the environment
                glm::vec3 irradiance(0.0f);
                const int sampleCount = environmentTexture.m_width; // Number of samples for integration
                for (int i = 0; i < sampleCount; ++i)
                {
                    glm::vec3 sampleDir = ImportanceSampleHemisphere(i, sampleCount, dir);
                    glm::vec3 sampleColor = SampleEnvironment(sampleDir, environmentTexture);
                    irradiance += sampleColor * glm::dot(sampleDir, dir); // Weight by cosine
                }
                irradiance /= float(sampleCount);

                // Store the irradiance result
                int dstIndex = (y * cubemapSize + x) * environmentTexture.m_components;
                assert(dstIndex + environmentTexture.m_components <= int(cubemapFace.size()));
                for (uint32_t c = 0; c < 3; ++c)
                {
                    irradianceTexture.m_data[face][dstIndex + c] = Float16(irradiance[c]);
                }
                irradianceTexture.m_data[face][dstIndex + 3] = Float16(1.0f);
            }
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    std::cout << "Generated irradiance texture (" << cubemapSize << "x" << cubemapSize
              << "x6) in " << elapsed.count() << " seconds" << std::endl;
}


} // namespace

//----------------------------------------------------------------------
// Environment Class Implementation

void Environment::Load(const std::string &filename)
{
    m_transform = glm::mat4(1.0f); // Reset the environment transformation matrix

    // Load the background texture
    LoadTexture(filename, m_backgroundTexture);

    // Load the irradiance texture
    GenerateIrradianceTexture(m_backgroundTexture, m_irradianceTexture);
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