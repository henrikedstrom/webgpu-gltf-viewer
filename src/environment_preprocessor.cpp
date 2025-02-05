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
    initComputePipelines();
}

void EnvironmentPreprocessor::GenerateIrradianceMap(const wgpu::Texture &environmentCubemap,
                                                    wgpu::Texture &outputCubemap)
{
    wgpu::TextureViewDescriptor inputViewDesc{
        .format = wgpu::TextureFormat::RGBA16Float,
        .dimension = wgpu::TextureViewDimension::Cube,
        .baseArrayLayer = 0,
        .arrayLayerCount = 6,
    };

    wgpu::TextureViewDescriptor outputViewDesc{
        .format = wgpu::TextureFormat::RGBA16Float,
        .dimension = wgpu::TextureViewDimension::e2DArray,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = 6,
    };

    // Bind group 0: Input parameters
    wgpu::BindGroupEntry bindGroup0Entries[] = {
        {.binding = 0, .sampler = m_environmentSampler},
        {.binding = 1, .textureView = environmentCubemap.CreateView(&inputViewDesc)},
    };
    wgpu::BindGroupDescriptor bindGroup0Descriptor{
        .layout = m_bindGroupLayouts[0],
        .entryCount = 2,
        .entries = bindGroup0Entries,
    };
    wgpu::BindGroup bindGroup0 = m_device.CreateBindGroup(&bindGroup0Descriptor);

    // NOTE: Bind group 1: Per-face input parameters is set dynamically in the loop below

    // Bind group 2: PrefilterParams buffer
    wgpu::BindGroupEntry bindGroup2Entries[] = {
        {.binding = 0, .buffer = m_prefilterParamsBuffers[0]},
    };
    wgpu::BindGroupDescriptor bindGroup2Descriptor{
        .layout = m_bindGroupLayouts[2],
        .entryCount = 1,
        .entries = bindGroup2Entries,
    };
    wgpu::BindGroup bindGroup2 = m_device.CreateBindGroup(&bindGroup2Descriptor);

    // Bind group 3: Output irradiance map
    wgpu::BindGroupEntry bindGroup3Entries[] = {
        {.binding = 0, .textureView = outputCubemap.CreateView(&outputViewDesc)},
    };
    wgpu::BindGroupDescriptor bindGroup3Descriptor{
        .layout = m_bindGroupLayouts[3],
        .entryCount = 1,
        .entries = bindGroup3Entries,
    };
    wgpu::BindGroup bindGroup3 = m_device.CreateBindGroup(&bindGroup3Descriptor);

    wgpu::Queue queue = m_device.GetQueue();
    wgpu::CommandEncoder encoder = m_device.CreateCommandEncoder();
    wgpu::ComputePassEncoder computePass = encoder.BeginComputePass();
    computePass.SetPipeline(m_pipelineIrradiance);

    // Set bind groups common to all faces
    computePass.SetBindGroup(0, bindGroup0, 0, nullptr);
    computePass.SetBindGroup(2, bindGroup2, 0, nullptr);
    computePass.SetBindGroup(3, bindGroup3, 0, nullptr);

    uint32_t numFaces = 6;
    for (uint32_t face = 0; face < numFaces; ++face)
    {
        // Update face index to compute shader
        computePass.SetBindGroup(1, m_perFaceBindGroups[face], 0, nullptr);

        constexpr uint32_t workgroupSize = 8;
        uint32_t workgroupCountX = (outputCubemap.GetWidth() + workgroupSize - 1) / workgroupSize;
        uint32_t workgroupCountY = (outputCubemap.GetHeight() + workgroupSize - 1) / workgroupSize;

        computePass.DispatchWorkgroups(workgroupCountX, workgroupCountY, 1);
    }

    computePass.End();
    wgpu::CommandBuffer commands = encoder.Finish();
    queue.Submit(1, &commands);
}

void EnvironmentPreprocessor::GeneratePrefilteredSpecularMap(const wgpu::Texture &environmentCubemap,
                                                             wgpu::Texture &outputCubemap)
{
    wgpu::TextureViewDescriptor inputViewDesc{
        .format = wgpu::TextureFormat::RGBA16Float,
        .dimension = wgpu::TextureViewDimension::Cube,
        .baseArrayLayer = 0,
        .arrayLayerCount = 6,
    };

    // Bind group 0: Input parameters
    wgpu::BindGroupEntry bindGroup0Entries[] = {
        {.binding = 0, .sampler = m_environmentSampler},
        {.binding = 1, .textureView = environmentCubemap.CreateView(&inputViewDesc)},
    };
    wgpu::BindGroupDescriptor bindGroup0Descriptor{
        .layout = m_bindGroupLayouts[0],
        .entryCount = 2,
        .entries = bindGroup0Entries,
    };
    wgpu::BindGroup bindGroup0 = m_device.CreateBindGroup(&bindGroup0Descriptor);

    wgpu::Queue queue = m_device.GetQueue();
    wgpu::CommandEncoder encoder = m_device.CreateCommandEncoder();
    wgpu::ComputePassEncoder computePass = encoder.BeginComputePass();
    computePass.SetPipeline(m_pipelinePrefilteredSpecular);

    // Set bind group0, common to all faces and mip levels
    computePass.SetBindGroup(0, bindGroup0, 0, nullptr);

    uint32_t numFaces = 6;
    for (uint32_t face = 0; face < numFaces; ++face)
    {
        // Update face index to compute shader
        computePass.SetBindGroup(1, m_perFaceBindGroups[face], 0, nullptr);

        uint32_t mipLevelCount = 10;
        for (uint32_t mipLevel = 0; mipLevel < mipLevelCount; ++mipLevel)
        {
            wgpu::TextureViewDescriptor outputViewDesc{
                .format = wgpu::TextureFormat::RGBA16Float,
                .dimension = wgpu::TextureViewDimension::e2DArray,
                .baseMipLevel = mipLevel,
                .mipLevelCount = 1,
                .baseArrayLayer = 0,
                .arrayLayerCount = 6,
            };

            // Bind group 2: PrefilterParams buffer - set roughness per mip level
            wgpu::BindGroupEntry bindGroup2Entries[] = {
                {.binding = 0, .buffer = m_prefilterParamsBuffers[mipLevel]},
            };
            wgpu::BindGroupDescriptor bindGroup2Descriptor{
                .layout = m_bindGroupLayouts[2],
                .entryCount = 1,
                .entries = bindGroup2Entries,
            };
            wgpu::BindGroup bindGroup2 = m_device.CreateBindGroup(&bindGroup2Descriptor);
            computePass.SetBindGroup(2, bindGroup2, 0, nullptr);

            // Bind group 3: Output specular map
            wgpu::BindGroupEntry bindGroup3Entries[] = {
                {.binding = 0, .textureView = outputCubemap.CreateView(&outputViewDesc)},
            };
            wgpu::BindGroupDescriptor bindGroup3Descriptor{
                .layout = m_bindGroupLayouts[3],
                .entryCount = 1,
                .entries = bindGroup3Entries,
            };
            wgpu::BindGroup bindGroup3 = m_device.CreateBindGroup(&bindGroup3Descriptor);
            computePass.SetBindGroup(3, bindGroup3, 0, nullptr);

            uint32_t mipWidth = std::max(1u, outputCubemap.GetWidth() >> mipLevel);
            uint32_t mipHeight = std::max(1u, outputCubemap.GetHeight() >> mipLevel);

            constexpr uint32_t workgroupSize = 8;
            uint32_t workgroupCountX = (mipWidth + workgroupSize - 1) / workgroupSize;
            uint32_t workgroupCountY = (mipHeight + workgroupSize - 1) / workgroupSize;

            computePass.DispatchWorkgroups(workgroupCountX, workgroupCountY, 1);
        }
    }
    computePass.End();
    wgpu::CommandBuffer commands = encoder.Finish();
    queue.Submit(1, &commands);
}

void EnvironmentPreprocessor::initUniformBuffers()
{
    // Create a buffer to store the prefilter parameters
    wgpu::BufferDescriptor bufferDescriptor{
        .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
        .size = sizeof(PrefilterParams),
    };

    for (uint32_t i = 0; i < 10; ++i)
    {
        m_prefilterParamsBuffers[i] = m_device.CreateBuffer(&bufferDescriptor);

        float roughness = static_cast<float>(i) / 9.0f;
        uint32_t numSamples = 1024u; // FIXME: Hardcoded number of samples

        // Initialize the buffer with default values.
        PrefilterParams params = {roughness, numSamples};
        m_device.GetQueue().WriteBuffer(m_prefilterParamsBuffers[i], 0, &params, sizeof(PrefilterParams));
    }

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
    // We use 4 bind groups:
    //  0: Input environment map
    //  1: Per-face input parameters (face index)
    //  2: Per-mip level prefilter parameters
    //  3: Output texture

    // --- Group 0: Input parameters ---
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

    wgpu::BindGroupLayoutEntry group0Entries[] = {samplerEntry, cubemapEntry};
    wgpu::BindGroupLayoutDescriptor group0LayoutDesc{
        .entryCount = 2,
        .entries = group0Entries,
    };
    m_bindGroupLayouts[0] = m_device.CreateBindGroupLayout(&group0LayoutDesc);

    // --- Group 1: Face index ---
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

    // --- Group 2: Prefilter parameters ---
    wgpu::BindGroupLayoutEntry prefilterParamsEntry{
        .binding = 0,
        .visibility = wgpu::ShaderStage::Compute,
        .buffer = {.type = wgpu::BufferBindingType::Uniform, .minBindingSize = sizeof(PrefilterParams)},
    };

    wgpu::BindGroupLayoutEntry group2Entries[] = {prefilterParamsEntry};
    wgpu::BindGroupLayoutDescriptor group2LayoutDesc{
        .entryCount = 1,
        .entries = group2Entries,
    };
    m_bindGroupLayouts[2] = m_device.CreateBindGroupLayout(&group2LayoutDesc);

    // --- Group 3: Output texture ---
    wgpu::BindGroupLayoutEntry outputTextureEntry{
        .binding = 0,
        .visibility = wgpu::ShaderStage::Compute,
        .storageTexture = {.access = wgpu::StorageTextureAccess::WriteOnly,
                           .format = wgpu::TextureFormat::RGBA16Float,
                           .viewDimension = wgpu::TextureViewDimension::e2DArray},
    };

    wgpu::BindGroupLayoutEntry group3Entries[] = {outputTextureEntry};
    wgpu::BindGroupLayoutDescriptor group3LayoutDesc{
        .entryCount = 1,
        .entries = group3Entries,
    };
    m_bindGroupLayouts[3] = m_device.CreateBindGroupLayout(&group3LayoutDesc);

    // Create bind groups for each face
    for (uint32_t face = 0; face < 6; ++face)
    {
        wgpu::BindGroupEntry bindGroupEntries[1] = {
            {.binding = 0, .buffer = m_perFaceUniformBuffers[face], .offset = 0, .size = sizeof(uint32_t)} // Face index
        };

        wgpu::BindGroupDescriptor bindGroupDescriptor{
            .layout = m_bindGroupLayouts[1], .entryCount = 1, .entries = bindGroupEntries};
        m_perFaceBindGroups[face] = m_device.CreateBindGroup(&bindGroupDescriptor);
    }

    // Create bind group for each mip level
    for (uint32_t mipLevel = 0; mipLevel < 10; ++mipLevel)
    {
        wgpu::BindGroupEntry bindGroupEntries[1] = {
            {.binding = 0,
             .buffer = m_prefilterParamsBuffers[mipLevel],
             .offset = 0,
             .size = sizeof(PrefilterParams)} // Prefilter parameters
        };

        wgpu::BindGroupDescriptor bindGroupDescriptor{
            .layout = m_bindGroupLayouts[2], .entryCount = 1, .entries = bindGroupEntries};
        m_prefilterParamsBindGroups[mipLevel] = m_device.CreateBindGroup(&bindGroupDescriptor);
    }
}

void EnvironmentPreprocessor::initComputePipelines()
{
    m_pipelineIrradiance = createComputePipeline("./assets/shaders/environment_prefilter.wgsl", "computeIrradiance");
    m_pipelinePrefilteredSpecular =
        createComputePipeline("./assets/shaders/environment_prefilter.wgsl", "computePrefilteredSpecular");
}

wgpu::ComputePipeline EnvironmentPreprocessor::createComputePipeline(const std::string &shaderPath,
                                                                     const std::string &entryPoint)
{
    std::string shaderCode = LoadShaderFile(shaderPath);

    wgpu::ShaderModuleWGSLDescriptor wgslDesc{};
    wgslDesc.code = shaderCode.c_str();

    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{.nextInChain = &wgslDesc};
    wgpu::ShaderModule computeShaderModule = m_device.CreateShaderModule(&shaderModuleDescriptor);

    wgpu::BindGroupLayout pipelineBindGroups[] = {
        m_bindGroupLayouts[0], // Group 0: input environment map
        m_bindGroupLayouts[1], // Group 1: face index uniform (per-face)
        m_bindGroupLayouts[2], // Group 2: input prefilter parameters
        m_bindGroupLayouts[3], // Group 3: output texture
    };
    wgpu::PipelineLayoutDescriptor layoutDescriptor{
        .bindGroupLayoutCount = 4,
        .bindGroupLayouts = pipelineBindGroups,
    };

    wgpu::PipelineLayout pipelineLayout = m_device.CreatePipelineLayout(&layoutDescriptor);

    wgpu::ComputePipelineDescriptor descriptor{
        .layout = pipelineLayout,
        .compute =
            {
                .module = computeShaderModule,
                .entryPoint = entryPoint.c_str(),
            },
    };

    return m_device.CreateComputePipeline(&descriptor);
}
