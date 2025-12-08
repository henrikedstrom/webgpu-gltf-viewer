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
    initUniformBuffers();
    initBindGroupLayouts();
    initComputePipelines();
}

void MipmapGenerator::GenerateMipmaps(const wgpu::Texture &texture, wgpu::Extent3D size, bool isCubeMap)
{
    uint32_t mipLevelCount = 1 + static_cast<uint32_t>(std::log2(std::max(size.width, size.height)));

    wgpu::TextureViewDescriptor viewDescriptor{};
    viewDescriptor.format = isCubeMap ? wgpu::TextureFormat::RGBA16Float : wgpu::TextureFormat::RGBA8Unorm;
    viewDescriptor.dimension = isCubeMap ? wgpu::TextureViewDimension::e2DArray : wgpu::TextureViewDimension::e2D;
    viewDescriptor.baseMipLevel = 0;
    viewDescriptor.mipLevelCount = 1; // Each view corresponds to a single mip level
    viewDescriptor.baseArrayLayer = 0;
    viewDescriptor.arrayLayerCount = isCubeMap ? 6u : 1u;

    std::vector<wgpu::TextureView> mipLevelViews(mipLevelCount);

    for (uint32_t i = 0; i < mipLevelCount; ++i)
    {
        viewDescriptor.baseMipLevel = i;
        mipLevelViews[i] = texture.CreateView(&viewDescriptor);
    }

    wgpu::Queue queue = m_device.GetQueue();
    wgpu::CommandEncoder encoder = m_device.CreateCommandEncoder();
    wgpu::ComputePassEncoder computePass = encoder.BeginComputePass();

    wgpu::ComputePipeline pipeline = isCubeMap ? m_pipelineCube : m_pipeline2D;
    computePass.SetPipeline(pipeline);

    wgpu::BindGroupLayout bindGroupLayout = isCubeMap ? m_bindGroupLayoutCube : m_bindGroupLayout2D;

    wgpu::BindGroupDescriptor bindGroupDescriptor{};
    bindGroupDescriptor.layout = bindGroupLayout;
    bindGroupDescriptor.entryCount = 2;

    wgpu::BindGroupEntry bindGroupEntries[2]{};
    bindGroupEntries[0].binding = 0; // Previous mip level
    bindGroupEntries[1].binding = 1; // Next mip level

    uint32_t numFaces = isCubeMap ? 6 : 1;

    for (uint32_t face = 0; face < numFaces; ++face)
    {
        if (isCubeMap)
        {
            // Set face index to compute shader
            computePass.SetBindGroup(1, m_faceBindGroups[face], 0, nullptr);
        }

        for (uint32_t nextLevel = 1; nextLevel < mipLevelViews.size(); ++nextLevel)
        {
            uint32_t width = std::max(1u, size.width >> nextLevel);
            uint32_t height = std::max(1u, size.height >> nextLevel);

            // Update input and output textures to compute shader
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
    }

    computePass.End();
    wgpu::CommandBuffer commands = encoder.Finish();
    queue.Submit(1, &commands);
}

void MipmapGenerator::initUniformBuffers()
{
    wgpu::BufferDescriptor bufferDescriptor{};
    bufferDescriptor.size = sizeof(uint32_t); // Face id
    bufferDescriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;

    for (uint32_t face = 0; face < 6; ++face)
    {
        m_uniformBuffers[face] = m_device.CreateBuffer(&bufferDescriptor);

        uint32_t faceIndexValue = face;
        m_device.GetQueue().WriteBuffer(m_uniformBuffers[face], 0, &faceIndexValue, sizeof(uint32_t));
    }
}

void MipmapGenerator::initBindGroupLayouts()
{
    // Common input texture layout
    wgpu::BindGroupLayoutEntry inputTexture{};
    inputTexture.binding = 0;
    inputTexture.visibility = wgpu::ShaderStage::Compute;
    inputTexture.texture.sampleType = wgpu::TextureSampleType::Float;
    inputTexture.texture.multisampled = false;

    // Common output texture layout
    wgpu::BindGroupLayoutEntry outputTexture{};
    outputTexture.binding = 1;
    outputTexture.visibility = wgpu::ShaderStage::Compute;
    outputTexture.storageTexture.access = wgpu::StorageTextureAccess::WriteOnly;

    // Setup 2D bind group layout
    inputTexture.texture.viewDimension = wgpu::TextureViewDimension::e2D;
    outputTexture.storageTexture.viewDimension = wgpu::TextureViewDimension::e2D;
    outputTexture.storageTexture.format = wgpu::TextureFormat::RGBA8Unorm;

    wgpu::BindGroupLayoutEntry entries2D[] = {inputTexture, outputTexture};
    wgpu::BindGroupLayoutDescriptor layoutDesc2D{};
    layoutDesc2D.entryCount = 2;
    layoutDesc2D.entries = entries2D;
    m_bindGroupLayout2D = m_device.CreateBindGroupLayout(&layoutDesc2D);

    // Setup Cube bind group layout
    inputTexture.texture.viewDimension = wgpu::TextureViewDimension::e2DArray;
    outputTexture.storageTexture.viewDimension = wgpu::TextureViewDimension::e2DArray;
    outputTexture.storageTexture.format = wgpu::TextureFormat::RGBA16Float;

    wgpu::BindGroupLayoutEntry entriesCube[] = {inputTexture, outputTexture};
    wgpu::BindGroupLayoutDescriptor layoutDescCube{};
    layoutDescCube.entryCount = 2;
    layoutDescCube.entries = entriesCube;
    m_bindGroupLayoutCube = m_device.CreateBindGroupLayout(&layoutDescCube);

    // Face index (only for cube maps)
    wgpu::BindGroupLayoutEntry faceIndex{};
    faceIndex.binding = 0;
    faceIndex.visibility = wgpu::ShaderStage::Compute;
    faceIndex.buffer.type = wgpu::BufferBindingType::Uniform;
    faceIndex.buffer.minBindingSize = 4;

    wgpu::BindGroupLayoutEntry entriesFace[] = {faceIndex};
    wgpu::BindGroupLayoutDescriptor layoutDescFace{};
    layoutDescFace.entryCount = 1;
    layoutDescFace.entries = entriesFace;
    m_bindGroupLayoutFace = m_device.CreateBindGroupLayout(&layoutDescFace);

    // Create bind groups for each face
    for (uint32_t face = 0; face < 6; ++face)
    {
        wgpu::BindGroupEntry bindGroupEntries[1]{};
        bindGroupEntries[0].binding = 0;
        bindGroupEntries[0].buffer = m_uniformBuffers[face];
        bindGroupEntries[0].offset = 0;
        bindGroupEntries[0].size = sizeof(uint32_t); // Face index

        wgpu::BindGroupDescriptor bindGroupDescriptor{};
        bindGroupDescriptor.layout = m_bindGroupLayoutFace;
        bindGroupDescriptor.entryCount = 1;
        bindGroupDescriptor.entries = bindGroupEntries;
        m_faceBindGroups[face] = m_device.CreateBindGroup(&bindGroupDescriptor);
    }
}

void MipmapGenerator::initComputePipelines()
{
    std::vector<wgpu::BindGroupLayout> layouts2D = {m_bindGroupLayout2D};
    std::vector<wgpu::BindGroupLayout> layoutsCube = {m_bindGroupLayoutCube, m_bindGroupLayoutFace};
    m_pipeline2D = createComputePipeline("./assets/shaders/mipmap_generator_2d.wgsl", layouts2D);
    m_pipelineCube = createComputePipeline("./assets/shaders/mipmap_generator_cube.wgsl", layoutsCube);
}

wgpu::ComputePipeline MipmapGenerator::createComputePipeline(const std::string &shaderPath,
                                                             const std::vector<wgpu::BindGroupLayout> &layouts)
{
    std::string shaderCode = LoadShaderFile(shaderPath);

    wgpu::ShaderModuleWGSLDescriptor wgslDesc{};
    wgslDesc.code = shaderCode.c_str();

    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{};
    shaderModuleDescriptor.nextInChain = &wgslDesc;
    wgpu::ShaderModule computeShaderModule = m_device.CreateShaderModule(&shaderModuleDescriptor);

    wgpu::PipelineLayoutDescriptor layoutDescriptor{};
    layoutDescriptor.bindGroupLayoutCount = static_cast<uint32_t>(layouts.size());
    layoutDescriptor.bindGroupLayouts = layouts.data();

    wgpu::PipelineLayout pipelineLayout = m_device.CreatePipelineLayout(&layoutDescriptor);

    wgpu::ComputePipelineDescriptor descriptor{};
    descriptor.layout = pipelineLayout;
    descriptor.compute.module = computeShaderModule;
    descriptor.compute.entryPoint = "computeMipMap";

    return m_device.CreateComputePipeline(&descriptor);
}
