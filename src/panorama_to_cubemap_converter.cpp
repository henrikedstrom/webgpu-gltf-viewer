// Standard Library Headers
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Project Headers
#include "panorama_to_cubemap_converter.h"

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
// PanoramaToCubemapConverter Class implementation

PanoramaToCubemapConverter::PanoramaToCubemapConverter(const wgpu::Device &device)
{
    m_device = device;
    InitUniformBuffers();
    InitSampler();
    InitBindGroupLayouts();
    InitBindGroups();
    InitComputePipeline();
}

void PanoramaToCubemapConverter::UploadAndConvert(const Environment::Texture &panoramaTextureInfo,
                                                  wgpu::Texture &environmentCubemap)
{
    uint32_t width = panoramaTextureInfo.m_width;
    uint32_t height = panoramaTextureInfo.m_height;
    const float *data = panoramaTextureInfo.m_data.data();

    // Create WebGPU texture descriptor for the input panorama texture
    wgpu::TextureDescriptor textureDescriptor{
        .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::StorageBinding | wgpu::TextureUsage::CopyDst |
                 wgpu::TextureUsage::CopySrc,
        .size = {width, height, 1},
        .format = wgpu::TextureFormat::RGBA32Float,
        .mipLevelCount = 1,
    };
    wgpu::Texture panoramaTexture = m_device.CreateTexture(&textureDescriptor);

    // Upload the texture data
    wgpu::Extent3D textureSize = {width, height, 1};
    wgpu::ImageCopyTexture imageCopyTexture{
        .texture = panoramaTexture,
        .mipLevel = 0,
        .origin = {0, 0, 0},
        .aspect = wgpu::TextureAspect::All,
    };
    wgpu::TextureDataLayout source{
        .offset = 0,
        .bytesPerRow = static_cast<uint32_t>(4 * width * sizeof(float)),
        .rowsPerImage = height,
    };
    m_device.GetQueue().WriteTexture(&imageCopyTexture, data, 4 * width * height * sizeof(float), &source,
                                     &textureSize);

    // Create views for the input panorama and output cubemap.
    wgpu::TextureViewDescriptor inputViewDesc{
        .format = wgpu::TextureFormat::RGBA32Float,
        .dimension = wgpu::TextureViewDimension::e2D,
        .baseArrayLayer = 0,
        .arrayLayerCount = 1,
    };
    wgpu::TextureViewDescriptor outputCubeViewDesc{
        .format = wgpu::TextureFormat::RGBA16Float,
        .dimension = wgpu::TextureViewDimension::e2DArray,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = 6,
    };

    // Bind group 0 - common for all faces
    wgpu::BindGroupEntry bindGroup0Entries[] = {
        {.binding = 0, .sampler = m_sampler},
        {.binding = 1, .textureView = panoramaTexture.CreateView(&inputViewDesc)},
        {.binding = 2, .textureView = environmentCubemap.CreateView(&outputCubeViewDesc)},
    };
    wgpu::BindGroupDescriptor bindGroup0Descriptor{
        .layout = m_bindGroupLayouts[0],
        .entryCount = 3,
        .entries = bindGroup0Entries,
    };
    wgpu::BindGroup bindGroup0 = m_device.CreateBindGroup(&bindGroup0Descriptor);

    // Create a command encoder and compute pass.
    wgpu::Queue queue = m_device.GetQueue();
    wgpu::CommandEncoder encoder = m_device.CreateCommandEncoder();
    wgpu::ComputePassEncoder computePass = encoder.BeginComputePass();

    // Set the compute pipeline for the conversion.
    computePass.SetPipeline(m_pipelineConvert);

    // Set bind groups common to all faces.
    computePass.SetBindGroup(0, bindGroup0, 0, nullptr);

    // Dispatch a compute shader for each face of the cubemap.
    constexpr uint32_t numFaces = 6;
    for (uint32_t face = 0; face < numFaces; ++face)
    {
        // For each face, update the per-face uniform (bind group 1).
        computePass.SetBindGroup(1, m_perFaceBindGroups[face], 0, nullptr);

        constexpr uint32_t workgroupSize = 8;
        uint32_t workgroupCountX = (environmentCubemap.GetWidth() + workgroupSize - 1) / workgroupSize;
        uint32_t workgroupCountY = (environmentCubemap.GetHeight() + workgroupSize - 1) / workgroupSize;
        computePass.DispatchWorkgroups(workgroupCountX, workgroupCountY, 1);
    }

    // Finish the compute pass and submit the command buffer.
    computePass.End();
    wgpu::CommandBuffer commands = encoder.Finish();
    queue.Submit(1, &commands);
}

void PanoramaToCubemapConverter::InitUniformBuffers()
{
    // Create a buffer for each face of the cubemap
    wgpu::BufferDescriptor bufferDescriptor{
        .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
        .size = sizeof(uint32_t),
    };

    for (uint32_t face = 0; face < 6; ++face)
    {
        m_perFaceUniformBuffers[face] = m_device.CreateBuffer(&bufferDescriptor);

        uint32_t faceIndexValue = face;
        m_device.GetQueue().WriteBuffer(m_perFaceUniformBuffers[face], 0, &faceIndexValue, sizeof(uint32_t));
    }
}

void PanoramaToCubemapConverter::InitSampler()
{
    wgpu::SamplerDescriptor samplerDescriptor{};
    samplerDescriptor.addressModeU = wgpu::AddressMode::Repeat;
    samplerDescriptor.addressModeV = wgpu::AddressMode::ClampToEdge;
    samplerDescriptor.addressModeW = wgpu::AddressMode::Repeat;
    samplerDescriptor.minFilter = wgpu::FilterMode::Nearest;
    samplerDescriptor.magFilter = wgpu::FilterMode::Nearest;
    samplerDescriptor.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
    m_sampler = m_device.CreateSampler(&samplerDescriptor);
}

void PanoramaToCubemapConverter::InitBindGroupLayouts()
{
    wgpu::BindGroupLayoutEntry samplerEntry{
        .binding = 0,
        .visibility = wgpu::ShaderStage::Compute,
        .sampler = {.type = wgpu::SamplerBindingType::NonFiltering},
    };

    wgpu::BindGroupLayoutEntry inputTextureEntry{
        .binding = 1,
        .visibility = wgpu::ShaderStage::Compute,
        .texture = {.sampleType = wgpu::TextureSampleType::UnfilterableFloat,
                    .viewDimension = wgpu::TextureViewDimension::e2D,
                    .multisampled = false},
    };

    wgpu::BindGroupLayoutEntry outputCubemapEntry{
        .binding = 2,
        .visibility = wgpu::ShaderStage::Compute,
        .storageTexture = {.access = wgpu::StorageTextureAccess::WriteOnly,
                           .format = wgpu::TextureFormat::RGBA16Float,
                           .viewDimension = wgpu::TextureViewDimension::e2DArray},
    };

    wgpu::BindGroupLayoutEntry group0Entries[] = {samplerEntry, inputTextureEntry, outputCubemapEntry};
    wgpu::BindGroupLayoutDescriptor group0LayoutDesc{
        .entryCount = 3,
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
}

void PanoramaToCubemapConverter::InitBindGroups()
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

void PanoramaToCubemapConverter::InitComputePipeline()
{
    std::string shaderCode = LoadShaderFile("./assets/shaders/panorama_to_cubemap.wgsl");

    wgpu::ShaderModuleWGSLDescriptor wgslDesc{};
    wgslDesc.code = shaderCode.c_str();

    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{.nextInChain = &wgslDesc};
    wgpu::ShaderModule computeShaderModule = m_device.CreateShaderModule(&shaderModuleDescriptor);

    wgpu::BindGroupLayout pipelineBindGroups[] = {
        m_bindGroupLayouts[0],
        m_bindGroupLayouts[1],
    };
    wgpu::PipelineLayoutDescriptor layoutDescriptor{
        .bindGroupLayoutCount = 2,
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

    descriptor.compute.entryPoint = "panoramaToCubemap";
    m_pipelineConvert = m_device.CreateComputePipeline(&descriptor);
}
