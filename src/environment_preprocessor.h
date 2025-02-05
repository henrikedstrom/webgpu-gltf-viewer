#pragma once

// Standard Library Headers
#include <cstdint>
#include <string>

// Third-Party Library Headers
#include <webgpu/webgpu_cpp.h>

// EnvironmentPreprocessor Class
class EnvironmentPreprocessor
{
  public:
    // Constructor
    explicit EnvironmentPreprocessor(const wgpu::Device &device);

    // Destructor
    ~EnvironmentPreprocessor() = default;

    // Rule of 5
    EnvironmentPreprocessor(const EnvironmentPreprocessor &) = delete;
    EnvironmentPreprocessor &operator=(const EnvironmentPreprocessor &) = delete;
    EnvironmentPreprocessor(EnvironmentPreprocessor &&) noexcept = default;
    EnvironmentPreprocessor &operator=(EnvironmentPreprocessor &&) noexcept = default;

    // Public Interface
    void GenerateIrradianceMap(const wgpu::Texture &environmentCubemap, wgpu::Texture &outputCubemap);
    void GeneratePrefilteredSpecularMap(const wgpu::Texture &environmentCubemap, wgpu::Texture &outputCubemap);

  private:
    // Types
    struct PrefilterParams
    {
        float roughness;
        uint32_t numSamples;
    };

    // Pipeline initialization
    void initUniformBuffers();
    void initSampler();
    void initBindGroupLayouts();
    void initComputePipelines();

    // Helper functions
    wgpu::ComputePipeline createComputePipeline(const std::string &shaderPath, const std::string &entryPoint);

    // WebGPU objects (initialized by constructor)
    wgpu::Device m_device;
    wgpu::PipelineLayout m_pipelineLayout;
    wgpu::BindGroupLayout m_bindGroupLayouts[4];
    wgpu::ComputePipeline m_pipelineIrradiance;
    wgpu::ComputePipeline m_pipelinePrefilteredSpecular;
    wgpu::Buffer m_prefilterParamsBuffers[10];
    wgpu::Buffer m_perFaceUniformBuffers[6];
    wgpu::BindGroup m_prefilterParamsBindGroups[10];
    wgpu::BindGroup m_perFaceBindGroups[6];
    wgpu::Sampler m_environmentSampler;
};
