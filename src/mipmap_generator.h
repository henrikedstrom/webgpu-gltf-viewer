#pragma once

// Standard Library Headers
#include <cstdint>
#include <string>

// Third-Party Library Headers
#include <webgpu/webgpu_cpp.h>

// MipmapGenerator Class
class MipmapGenerator
{
  public:
    // Constructor
    explicit MipmapGenerator(const wgpu::Device &device);

    // Destructor
    ~MipmapGenerator() = default;

    // Rule of 5
    MipmapGenerator(const MipmapGenerator &) = delete;
    MipmapGenerator &operator=(const MipmapGenerator &) = delete;
    MipmapGenerator(MipmapGenerator &&) noexcept = default;
    MipmapGenerator &operator=(MipmapGenerator &&) noexcept = default;

    // Public Interface
    void GenerateMipmaps(const wgpu::Texture &texture, wgpu::Extent3D size, bool isCubeMap);

  private:
    // Pipeline initialization
    void initUniformBuffers();
    void initBindGroupLayouts();
    void initComputePipelines();

    // Helper functions
    wgpu::ComputePipeline createComputePipeline(const std::string &shaderPath,
                                                const std::vector<wgpu::BindGroupLayout> &layouts);

    // WebGPU objects (initialized by constructor)
    wgpu::Device m_device;
    wgpu::PipelineLayout m_pipelineLayout;
    wgpu::BindGroupLayout m_bindGroupLayout2D;
    wgpu::BindGroupLayout m_bindGroupLayoutCube;
    wgpu::BindGroupLayout m_bindGroupLayoutFace;
    wgpu::ComputePipeline m_pipeline2D;
    wgpu::ComputePipeline m_pipelineCube;
    wgpu::Buffer m_uniformBuffers[6];
    wgpu::BindGroup m_faceBindGroups[6];
};
