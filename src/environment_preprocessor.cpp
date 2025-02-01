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

void EnvironmentPreprocessor::GenerateIrradianceMap(const wgpu::Texture &environmentCubemap, wgpu::Texture &outputCubemap)
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

    wgpu::BindGroupEntry bindGroupEntries[] = {
        {.binding = 0, .sampler = m_environmentSampler},
        {.binding = 1, .textureView = environmentCubemap.CreateView(&inputViewDesc)},
        {.binding = 2, .textureView = outputCubemap.CreateView(&outputViewDesc)},
    };
    wgpu::BindGroupDescriptor bindGroupDescriptor{
        .layout = m_bindGroupLayout,
        .entryCount = 3,
        .entries = bindGroupEntries,
    };

    wgpu::Queue queue = m_device.GetQueue();
    wgpu::CommandEncoder encoder = m_device.CreateCommandEncoder();
    wgpu::ComputePassEncoder computePass = encoder.BeginComputePass();
    computePass.SetPipeline(m_pipelineIrradiance);

    wgpu::BindGroup bindGroup = m_device.CreateBindGroup(&bindGroupDescriptor);
    computePass.SetBindGroup(0, bindGroup, 0, nullptr);

    uint32_t numFaces = 6;
    for (uint32_t face = 0; face < numFaces; ++face)
    {
        // Update face index to compute shader
        computePass.SetBindGroup(1, m_faceBindGroups[face], 0, nullptr);

        constexpr uint32_t workgroupSize = 8;
        uint32_t workgroupCountX = (outputCubemap.GetWidth() + workgroupSize - 1) / workgroupSize;
        uint32_t workgroupCountY = (outputCubemap.GetHeight() + workgroupSize - 1) / workgroupSize;

        computePass.DispatchWorkgroups(workgroupCountX, workgroupCountY, 1);
    }

    computePass.End();
    wgpu::CommandBuffer commands = encoder.Finish();
    queue.Submit(1, &commands);
}

void EnvironmentPreprocessor::initUniformBuffers()
{
    wgpu::BufferDescriptor bufferDescriptor{
        .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
        .size = sizeof(uint32_t), // Face id
    };

    for (uint32_t face = 0; face < 6; ++face)
    {
        m_uniformBuffers[face] = m_device.CreateBuffer(&bufferDescriptor);

        uint32_t faceIndexValue = face;
        m_device.GetQueue().WriteBuffer(m_uniformBuffers[face], 0, &faceIndexValue, sizeof(uint32_t));
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
    wgpu::BindGroupLayoutEntry inputeSampler{
        .binding = 0,
        .visibility = wgpu::ShaderStage::Compute,
        .sampler = {.type = wgpu::SamplerBindingType::Filtering},
    };

    wgpu::BindGroupLayoutEntry inputCubemap{
        .binding = 1,
        .visibility = wgpu::ShaderStage::Compute,
        .texture = {.sampleType = wgpu::TextureSampleType::Float,
                    .viewDimension = wgpu::TextureViewDimension::Cube,
                    .multisampled = false},
    };

    wgpu::BindGroupLayoutEntry outputCubemap{
        .binding = 2,
        .visibility = wgpu::ShaderStage::Compute,
        .storageTexture = {.access = wgpu::StorageTextureAccess::WriteOnly,
                           .format = wgpu::TextureFormat::RGBA16Float,
                           .viewDimension = wgpu::TextureViewDimension::e2DArray},
    };

    wgpu::BindGroupLayoutEntry entries[] = {inputeSampler, inputCubemap, outputCubemap};
    wgpu::BindGroupLayoutDescriptor layoutDesc{.entryCount = 3, .entries = entries};
    m_bindGroupLayout = m_device.CreateBindGroupLayout(&layoutDesc);

    wgpu::BindGroupLayoutEntry faceIndex{.binding = 0,
                                         .visibility = wgpu::ShaderStage::Compute,
                                         .buffer = {.type = wgpu::BufferBindingType::Uniform, .minBindingSize = 4}};

    wgpu::BindGroupLayoutEntry entriesFace[] = {faceIndex};
    wgpu::BindGroupLayoutDescriptor layoutDescFace{.entryCount = 1, .entries = entriesFace};
    m_bindGroupLayoutFace = m_device.CreateBindGroupLayout(&layoutDescFace);

    // Create bind groups for each face
    for (uint32_t face = 0; face < 6; ++face)
    {
        wgpu::BindGroupEntry bindGroupEntries[1] = {
            {.binding = 0, .buffer = m_uniformBuffers[face], .offset = 0, .size = sizeof(uint32_t)} // Face index
        };

        wgpu::BindGroupDescriptor bindGroupDescriptor{
            .layout = m_bindGroupLayoutFace, .entryCount = 1, .entries = bindGroupEntries};
        m_faceBindGroups[face] = m_device.CreateBindGroup(&bindGroupDescriptor);
    }
}

void EnvironmentPreprocessor::initComputePipelines()
{
    m_pipelineIrradiance = createComputePipeline("./assets/shaders/irradiance_map.wgsl", "computeIrradiance", m_bindGroupLayout);
    //m_pipelinePrefilteredSpecular = createComputePipeline("./assets/shaders/prefiltered_specular.wgsl", m_bindGroupLayout);
}

wgpu::ComputePipeline EnvironmentPreprocessor::createComputePipeline(const std::string &shaderPath,
                                                                     const std::string &entryPoint,
                                                                     wgpu::BindGroupLayout layout)
{
    std::string shaderCode = LoadShaderFile(shaderPath);

    wgpu::ShaderModuleWGSLDescriptor wgslDesc{};
    wgslDesc.code = shaderCode.c_str();

    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{.nextInChain = &wgslDesc};
    wgpu::ShaderModule computeShaderModule = m_device.CreateShaderModule(&shaderModuleDescriptor);

    wgpu::BindGroupLayout layouts[] = {layout, m_bindGroupLayoutFace};
    wgpu::PipelineLayoutDescriptor layoutDescriptor{
        .bindGroupLayoutCount = 2,
        .bindGroupLayouts = layouts,
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

