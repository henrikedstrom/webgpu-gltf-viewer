// Standard Library Headers
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Project Headers
#include "environment_preprocessor.h"

//----------------------------------------------------------------------
// Internal Utility Functions

namespace
{

// TODO: Move to helper class for managing resources
std::string LoadShaderFile(const std::string &filepath)
{
    std::ifstream file(filepath, std::ios::in | std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "Failed to open shader file: " + filepath + "\n";
        return "";
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace

//----------------------------------------------------------------------
// EnvironmentPreprocessor Class implementation

EnvironmentPreprocessor::EnvironmentPreprocessor(const wgpu::Device &device)
{
    m_device = device;
    initUniformBuffers();
    initSampler();
    initBindGroupLayouts();
    initBindGroups();
    initComputePipelines();
}

void EnvironmentPreprocessor::GenerateMaps(const wgpu::Texture &environmentCubemap, wgpu::Texture &irradianceCubemap,
                                           wgpu::Texture &prefilteredSpecularCubemap, wgpu::Texture &brdfIntegrationLUT)
{
    // Create views for the input cubemap and output cubemap.
    wgpu::TextureViewDescriptor inputViewDesc{
        .format = wgpu::TextureFormat::RGBA16Float,
        .dimension = wgpu::TextureViewDimension::Cube,
        .baseArrayLayer = 0,
        .arrayLayerCount = 6,
    };
    wgpu::TextureViewDescriptor outputCubeViewDesc{
        .format = wgpu::TextureFormat::RGBA16Float,
        .dimension = wgpu::TextureViewDimension::e2DArray,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = 6,
    };
    wgpu::TextureViewDescriptor output2DViewDesc{
        .format = wgpu::TextureFormat::RGBA16Float,
        .dimension = wgpu::TextureViewDimension::e2D,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = 1,
    };

    // Bind group 0 (common for all passes)
    wgpu::BindGroupEntry bindGroup0Entries[] = {
        {.binding = 0, .sampler = m_environmentSampler},
        {.binding = 1, .textureView = environmentCubemap.CreateView(&inputViewDesc)},
        {.binding = 2, .buffer = m_uniformBuffer},
        {.binding = 3, .textureView = irradianceCubemap.CreateView(&outputCubeViewDesc)},
        {.binding = 4, .textureView = brdfIntegrationLUT.CreateView(&output2DViewDesc)},
    };
    wgpu::BindGroupDescriptor bindGroup0Descriptor{
        .layout = m_bindGroupLayouts[0],
        .entryCount = 5,
        .entries = bindGroup0Entries,
    };
    wgpu::BindGroup bindGroup0 = m_device.CreateBindGroup(&bindGroup0Descriptor);

    // Bind group 2 (per-mip)
    createPerMipBindGroups(prefilteredSpecularCubemap);

    // Create a command encoder and compute pass.
    wgpu::Queue queue = m_device.GetQueue();
    wgpu::CommandEncoder encoder = m_device.CreateCommandEncoder();
    wgpu::ComputePassEncoder computePass = encoder.BeginComputePass();

    // ---- Pass 1: Generate Irradiance Map (Diffuse IBL) ----

    // Set the pipeline for irradiance cubemap generation.
    computePass.SetPipeline(m_pipelineIrradiance);

    // Set bind groups common to all faces.
    computePass.SetBindGroup(0, bindGroup0, 0, nullptr);
    computePass.SetBindGroup(2, m_perMipBindGroups[0], 0, nullptr); // Make sure BG2 is valid (not used in first pass)

    // Dispatch a compute shader for each face of the cubemap.
    constexpr uint32_t numFaces = 6;
    for (uint32_t face = 0; face < numFaces; ++face)
    {
        // For each face, update the per-face uniform (bind group 1).
        computePass.SetBindGroup(1, m_perFaceBindGroups[face], 0, nullptr);

        constexpr uint32_t workgroupSize = 8;
        uint32_t workgroupCountX = (irradianceCubemap.GetWidth() + workgroupSize - 1) / workgroupSize;
        uint32_t workgroupCountY = (irradianceCubemap.GetHeight() + workgroupSize - 1) / workgroupSize;
        computePass.DispatchWorkgroups(workgroupCountX, workgroupCountY, 1);
    }

    // ---- Pass 2: Generate Prefiltered Specular Map (Specular IBL) ----

    const uint32_t mipLevelCount = prefilteredSpecularCubemap.GetMipLevelCount();

    // Set the pipeline for prefiltered specular cubemap generation.
    computePass.SetPipeline(m_pipelinePrefilteredSpecular);

    // Dispatch a compute shader for each mip level of each face of the cubemap.
    for (uint32_t face = 0; face < numFaces; ++face)
    {
        // Bind per-face uniform (bind group 1).
        computePass.SetBindGroup(1, m_perFaceBindGroups[face], 0, nullptr);

        for (uint32_t mipLevel = 0; mipLevel < mipLevelCount; ++mipLevel)
        {
            // Bind per-mip uniforms (bind group 2).
            computePass.SetBindGroup(2, m_perMipBindGroups[mipLevel], 0, nullptr);

            uint32_t mipWidth = std::max(1u, prefilteredSpecularCubemap.GetWidth() >> mipLevel);
            uint32_t mipHeight = std::max(1u, prefilteredSpecularCubemap.GetHeight() >> mipLevel);

            constexpr uint32_t workgroupSize = 8;
            uint32_t workgroupCountX = (mipWidth + workgroupSize - 1) / workgroupSize;
            uint32_t workgroupCountY = (mipHeight + workgroupSize - 1) / workgroupSize;

            computePass.DispatchWorkgroups(workgroupCountX, workgroupCountY, 1);
        }
    }

    // ---- Pass 3: Generate BRDF Integration LUT ----

    // Set the pipeline for BRDF integration LUT generation.
    computePass.SetPipeline(m_pipelineBRDFIntegrationLUT);

    // Dispatch a compute shader for the output texture.
    uint32_t width = brdfIntegrationLUT.GetWidth();
    uint32_t height = brdfIntegrationLUT.GetHeight();
    constexpr uint32_t workgroupSize = 8;
    uint32_t workgroupCountX = (width + workgroupSize - 1) / workgroupSize;
    uint32_t workgroupCountY = (height + workgroupSize - 1) / workgroupSize;
    computePass.DispatchWorkgroups(workgroupCountX, workgroupCountY, 1);

    // Finish the compute pass and submit the command buffer.
    computePass.End();
    wgpu::CommandBuffer commands = encoder.Finish();
    queue.Submit(1, &commands);
}

void EnvironmentPreprocessor::initUniformBuffers()
{
    // Create a buffer for the prefilter parameters
    wgpu::BufferDescriptor bufferDescriptor{
        .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
        .size = sizeof(uint32_t),
    };

    m_uniformBuffer = m_device.CreateBuffer(&bufferDescriptor);
    uint32_t numSamples = 1024; // FIXME: Hardcoded number of samples
    m_device.GetQueue().WriteBuffer(m_uniformBuffer, 0, &numSamples, sizeof(uint32_t));

    // Update descriptor for per-face uniform buffers
    bufferDescriptor.size = sizeof(uint32_t); // Face id

    for (uint32_t face = 0; face < 6; ++face)
    {
        m_perFaceUniformBuffers[face] = m_device.CreateBuffer(&bufferDescriptor);

        uint32_t faceIndexValue = face;
        m_device.GetQueue().WriteBuffer(m_perFaceUniformBuffers[face], 0, &faceIndexValue, sizeof(uint32_t));
    }
}

void EnvironmentPreprocessor::initSampler()
{
    wgpu::SamplerDescriptor samplerDescriptor{};
    samplerDescriptor.addressModeU = wgpu::AddressMode::Repeat;
    samplerDescriptor.addressModeV = wgpu::AddressMode::Repeat;
    samplerDescriptor.addressModeW = wgpu::AddressMode::Repeat;
    samplerDescriptor.minFilter = wgpu::FilterMode::Linear;
    samplerDescriptor.magFilter = wgpu::FilterMode::Linear;
    samplerDescriptor.mipmapFilter = wgpu::MipmapFilterMode::Linear;
    m_environmentSampler = m_device.CreateSampler(&samplerDescriptor);
}

void EnvironmentPreprocessor::initBindGroupLayouts()
{
    wgpu::BindGroupLayoutEntry samplerEntry{
        .binding = 0,
        .visibility = wgpu::ShaderStage::Compute,
        .sampler = {.type = wgpu::SamplerBindingType::Filtering},
    };

    wgpu::BindGroupLayoutEntry cubemapEntry{
        .binding = 1,
        .visibility = wgpu::ShaderStage::Compute,
        .texture = {.sampleType = wgpu::TextureSampleType::Float,
                    .viewDimension = wgpu::TextureViewDimension::Cube,
                    .multisampled = false},
    };

    wgpu::BindGroupLayoutEntry numSamplesEntry{
        .binding = 2,
        .visibility = wgpu::ShaderStage::Compute,
        .buffer = {.type = wgpu::BufferBindingType::Uniform, .minBindingSize = sizeof(uint32_t)},
    };

    wgpu::BindGroupLayoutEntry irradinaceEntry{
        .binding = 3,
        .visibility = wgpu::ShaderStage::Compute,
        .storageTexture = {.access = wgpu::StorageTextureAccess::WriteOnly,
                           .format = wgpu::TextureFormat::RGBA16Float,
                           .viewDimension = wgpu::TextureViewDimension::e2DArray},
    };

    wgpu::BindGroupLayoutEntry brdfLutEntry{
        .binding = 4,
        .visibility = wgpu::ShaderStage::Compute,
        .storageTexture = {.access = wgpu::StorageTextureAccess::WriteOnly,
                           .format = wgpu::TextureFormat::RGBA16Float,
                           .viewDimension = wgpu::TextureViewDimension::e2D},
    };

    wgpu::BindGroupLayoutEntry group0Entries[] = {samplerEntry, cubemapEntry, numSamplesEntry, irradinaceEntry,
                                                  brdfLutEntry};
    wgpu::BindGroupLayoutDescriptor group0LayoutDesc{
        .entryCount = 5,
        .entries = group0Entries,
    };
    m_bindGroupLayouts[0] = m_device.CreateBindGroupLayout(&group0LayoutDesc);

    wgpu::BindGroupLayoutEntry faceIndexEntry{
        .binding = 0,
        .visibility = wgpu::ShaderStage::Compute,
        .buffer = {.type = wgpu::BufferBindingType::Uniform, .minBindingSize = sizeof(uint32_t)},
    };

    wgpu::BindGroupLayoutEntry group1Entries[] = {faceIndexEntry};
    wgpu::BindGroupLayoutDescriptor group1LayoutDesc{
        .entryCount = 1,
        .entries = group1Entries,
    };
    m_bindGroupLayouts[1] = m_device.CreateBindGroupLayout(&group1LayoutDesc);

    wgpu::BindGroupLayoutEntry roughnessParamsEntry{
        .binding = 0,
        .visibility = wgpu::ShaderStage::Compute,
        .buffer = {.type = wgpu::BufferBindingType::Uniform, .minBindingSize = sizeof(float)},
    };

    wgpu::BindGroupLayoutEntry prefilteredSpecularEntry{
        .binding = 1,
        .visibility = wgpu::ShaderStage::Compute,
        .storageTexture = {.access = wgpu::StorageTextureAccess::WriteOnly,
                           .format = wgpu::TextureFormat::RGBA16Float,
                           .viewDimension = wgpu::TextureViewDimension::e2DArray},
    };

    wgpu::BindGroupLayoutEntry group2Entries[] = {roughnessParamsEntry, prefilteredSpecularEntry};
    wgpu::BindGroupLayoutDescriptor group2LayoutDesc{
        .entryCount = 2,
        .entries = group2Entries,
    };
    m_bindGroupLayouts[2] = m_device.CreateBindGroupLayout(&group2LayoutDesc);
}

void EnvironmentPreprocessor::initBindGroups()
{
    // Create bind groups for per-face uniform buffers
    for (uint32_t face = 0; face < 6; ++face)
    {
        wgpu::BindGroupEntry bindGroupEntries[] = {
            {.binding = 0, .buffer = m_perFaceUniformBuffers[face]},
        };
        wgpu::BindGroupDescriptor bindGroupDescriptor{
            .layout = m_bindGroupLayouts[1],
            .entryCount = 1,
            .entries = bindGroupEntries,
        };
        m_perFaceBindGroups[face] = m_device.CreateBindGroup(&bindGroupDescriptor);
    }
}

void EnvironmentPreprocessor::initComputePipelines()
{
    std::string shaderCode = LoadShaderFile("./assets/shaders/environment_prefilter.wgsl");

    wgpu::ShaderModuleWGSLDescriptor wgslDesc{};
    wgslDesc.code = shaderCode.c_str();

    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{.nextInChain = &wgslDesc};
    wgpu::ShaderModule computeShaderModule = m_device.CreateShaderModule(&shaderModuleDescriptor);

    wgpu::BindGroupLayout pipelineBindGroups[] = {
        m_bindGroupLayouts[0],
        m_bindGroupLayouts[1],
        m_bindGroupLayouts[2],
    };
    wgpu::PipelineLayoutDescriptor layoutDescriptor{
        .bindGroupLayoutCount = 3,
        .bindGroupLayouts = pipelineBindGroups,
    };

    wgpu::PipelineLayout pipelineLayout = m_device.CreatePipelineLayout(&layoutDescriptor);

    wgpu::ComputePipelineDescriptor descriptor{
        .layout = pipelineLayout,
        .compute =
            {
                .module = computeShaderModule,
            },
    };

    descriptor.compute.entryPoint = "computeIrradiance";
    m_pipelineIrradiance = m_device.CreateComputePipeline(&descriptor);

    descriptor.compute.entryPoint = "computePrefilteredSpecular";
    m_pipelinePrefilteredSpecular = m_device.CreateComputePipeline(&descriptor);

    descriptor.compute.entryPoint = "computeLUT";
    m_pipelineBRDFIntegrationLUT = m_device.CreateComputePipeline(&descriptor);
}

void EnvironmentPreprocessor::createPerMipBindGroups(const wgpu::Texture &prefilteredSpecularCubemap)
{
    const uint32_t mipLevelCount = prefilteredSpecularCubemap.GetMipLevelCount();

    m_perMipUniformBuffers.resize(mipLevelCount);
    m_perMipBindGroups.resize(mipLevelCount);

    // Create a buffer for the roughness parameter
    wgpu::BufferDescriptor bufferDescriptor{
        .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
        .size = sizeof(float),
    };
    for (uint32_t i = 0; i < mipLevelCount; ++i)
    {
        m_perMipUniformBuffers[i] = m_device.CreateBuffer(&bufferDescriptor);
        float roughness = static_cast<float>(i) / static_cast<float>(mipLevelCount - 1);
        m_device.GetQueue().WriteBuffer(m_perMipUniformBuffers[i], 0, &roughness, sizeof(roughness));
    }

    // Create a texture view descriptor for the output cubemap
    wgpu::TextureViewDescriptor outputCubeViewDesc{
        .format = wgpu::TextureFormat::RGBA16Float,
        .dimension = wgpu::TextureViewDimension::e2DArray,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = 6,
    };

    // Create bind group descriptor
    wgpu::BindGroupEntry bindGroup2Entries[] = {
        {
            .binding = 0,
        },
        {
            .binding = 1,
        },
    };
    wgpu::BindGroupDescriptor bindGroup2Descriptor{
        .layout = m_bindGroupLayouts[2],
        .entryCount = 2,
        .entries = bindGroup2Entries,
    };

    // Create bind groups for each mip level
    for (uint32_t mipLevel = 0; mipLevel < mipLevelCount; ++mipLevel)
    {
        // Update per-mip bind group (bind group 2).
        outputCubeViewDesc.baseMipLevel = mipLevel;
        bindGroup2Entries[0].buffer = m_perMipUniformBuffers[mipLevel];
        bindGroup2Entries[1].textureView = prefilteredSpecularCubemap.CreateView(&outputCubeViewDesc);
        m_perMipBindGroups[mipLevel] = m_device.CreateBindGroup(&bindGroup2Descriptor);
    }
}
