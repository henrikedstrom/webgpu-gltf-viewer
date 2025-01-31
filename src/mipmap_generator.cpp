// Standard Library Headers
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Project Headers
#include "mipmap_generator.h"

//----------------------------------------------------------------------
// Internal Utility Functions

namespace
{

struct MipLevelData
{
    wgpu::TextureView view;
    wgpu::Extent3D size;
};

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
// MipmapGenerator Class implementation

MipmapGenerator::MipmapGenerator(const wgpu::Device &device)
{
    m_device = device;
    initBindGroupLayouts();
    initComputePipelines();
    initUniformBuffers();
}

void MipmapGenerator::GenerateMipmaps(const wgpu::Texture &texture, wgpu::Extent3D size, bool isCubeMap)
{
    uint32_t mipLevelCount = 1 + static_cast<uint32_t>(std::log2(std::max(size.width, size.height)));

    wgpu::TextureViewDescriptor viewDescriptor{
        .format = isCubeMap ? wgpu::TextureFormat::RGBA16Float : wgpu::TextureFormat::RGBA8Unorm,
        .dimension = isCubeMap ? wgpu::TextureViewDimension::e2DArray : wgpu::TextureViewDimension::e2D,
        .baseMipLevel = 0,
        .mipLevelCount = 1, // Each view corresponds to a single mip level
        .baseArrayLayer = 0,
        .arrayLayerCount = isCubeMap ? 6u : 1u,
    };

    std::vector<wgpu::TextureView> mipLevelViews(mipLevelCount);

    for (uint32_t i = 0; i < mipLevelCount; ++i)
    {
        viewDescriptor.baseMipLevel = i;
        mipLevelViews[i] = texture.CreateView(&viewDescriptor);
    }

    wgpu::Queue queue = m_device.GetQueue();

    wgpu::ComputePipeline pipeline = isCubeMap ? m_pipelineCube : m_pipeline2D;
    wgpu::BindGroupLayout bindGroupLayout = isCubeMap ? m_bindGroupLayoutCube : m_bindGroupLayout2D;

    wgpu::BindGroupDescriptor bindGroupDescriptor{
        .layout = bindGroupLayout,
        .entryCount = isCubeMap ? 3u : 2u, // Face index only used for cube maps
    };

    wgpu::BindGroupEntry bindGroupEntries[3] = {
        {.binding = 0},                                                               // Previous mip level
        {.binding = 1},                                                               // Next mip level
        {.binding = 2, .buffer = m_uniformBuffer, .offset = 0, .size = sizeof(float)} // Face index
    };

    uint32_t numFaces = isCubeMap ? 6 : 1;

    for (uint32_t face = 0; face < numFaces; ++face)
    {
        if (isCubeMap)
        {
            queue.WriteBuffer(m_uniformBuffer, 0, &face, sizeof(face));
        }

        wgpu::CommandEncoder encoder = m_device.CreateCommandEncoder();
        wgpu::ComputePassEncoder computePass = encoder.BeginComputePass();
        computePass.SetPipeline(pipeline);

        for (uint32_t nextLevel = 1; nextLevel < mipLevelViews.size(); ++nextLevel)
        {
            uint32_t width = std::max(1u, size.width >> nextLevel);
            uint32_t height = std::max(1u, size.height >> nextLevel);

            bindGroupEntries[0].textureView = mipLevelViews[nextLevel - 1];
            bindGroupEntries[1].textureView = mipLevelViews[nextLevel];
            bindGroupDescriptor.entries = bindGroupEntries;

            wgpu::BindGroup bindGroup = m_device.CreateBindGroup(&bindGroupDescriptor);
            computePass.SetBindGroup(0, bindGroup, 0, nullptr);

            constexpr uint32_t workgroupSize = 8;
            uint32_t workgroupCountX = (width + workgroupSize - 1) / workgroupSize;
            uint32_t workgroupCountY = (height + workgroupSize - 1) / workgroupSize;

            computePass.DispatchWorkgroups(workgroupCountX, workgroupCountY, 1);
        }

        computePass.End();
        wgpu::CommandBuffer commands = encoder.Finish();
        queue.Submit(1, &commands);
    }
}

void MipmapGenerator::initBindGroupLayouts()
{
    // Common input texture layout
    wgpu::BindGroupLayoutEntry inputTexture{
        .binding = 0,
        .visibility = wgpu::ShaderStage::Compute,
        .texture = {.sampleType = wgpu::TextureSampleType::Float,
                    .multisampled = false}
    };

    // Common output texture layout
    wgpu::BindGroupLayoutEntry outputTexture{
        .binding = 1,
        .visibility = wgpu::ShaderStage::Compute,
        .storageTexture = {.access = wgpu::StorageTextureAccess::WriteOnly}
    };

    // Face index (only for cube maps)
    wgpu::BindGroupLayoutEntry faceIndex{
        .binding = 2,
        .visibility = wgpu::ShaderStage::Compute,
        .buffer = {.type = wgpu::BufferBindingType::Uniform, .minBindingSize = 4}
    };

    // Setup 2D bind group layout
    inputTexture.texture.viewDimension = wgpu::TextureViewDimension::e2D;
    outputTexture.storageTexture.viewDimension = wgpu::TextureViewDimension::e2D;
    outputTexture.storageTexture.format = wgpu::TextureFormat::RGBA8Unorm;

    wgpu::BindGroupLayoutEntry entries2D[] = {inputTexture, outputTexture};
    wgpu::BindGroupLayoutDescriptor layoutDesc2D{.entryCount = 2, .entries = entries2D};
    m_bindGroupLayout2D = m_device.CreateBindGroupLayout(&layoutDesc2D);

    // Setup Cube bind group layout
    inputTexture.texture.viewDimension = wgpu::TextureViewDimension::e2DArray;
    outputTexture.storageTexture.viewDimension = wgpu::TextureViewDimension::e2DArray;
    outputTexture.storageTexture.format = wgpu::TextureFormat::RGBA16Float;

    wgpu::BindGroupLayoutEntry entriesCube[] = {inputTexture, outputTexture, faceIndex};
    wgpu::BindGroupLayoutDescriptor layoutDescCube{.entryCount = 3, .entries = entriesCube};
    m_bindGroupLayoutCube = m_device.CreateBindGroupLayout(&layoutDescCube);
}

void MipmapGenerator::initComputePipelines()
{
    m_pipeline2D = createComputePipeline("./assets/shaders/mipmap_generator_2d.wgsl", m_bindGroupLayout2D);
    m_pipelineCube = createComputePipeline("./assets/shaders/mipmap_generator_cube.wgsl", m_bindGroupLayoutCube);
}

void MipmapGenerator::initUniformBuffers()
{
    wgpu::BufferDescriptor bufferDescriptor{};
    bufferDescriptor.size = sizeof(float); // Face id
    bufferDescriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;

    m_uniformBuffer = m_device.CreateBuffer(&bufferDescriptor);
}

wgpu::ComputePipeline MipmapGenerator::createComputePipeline(const std::string &shaderPath,
                                                             wgpu::BindGroupLayout layout)
{
    std::string shaderCode = LoadShaderFile(shaderPath);

    wgpu::ShaderModuleWGSLDescriptor wgslDesc{};
    wgslDesc.code = shaderCode.c_str();

    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{.nextInChain = &wgslDesc};
    wgpu::ShaderModule computeShaderModule = m_device.CreateShaderModule(&shaderModuleDescriptor);

    wgpu::PipelineLayoutDescriptor layoutDescriptor{
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &layout,
    };

    wgpu::PipelineLayout pipelineLayout = m_device.CreatePipelineLayout(&layoutDescriptor);

    wgpu::ComputePipelineDescriptor descriptor{
        .layout = pipelineLayout,
        .compute =
            {
                .module = computeShaderModule,
                .entryPoint = "computeMipMap",
            },
    };

    return m_device.CreateComputePipeline(&descriptor);
}
