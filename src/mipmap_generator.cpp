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

namespace {

// TODO: Move to helper class for managing resources
std::string LoadShaderFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
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

MipmapGenerator::MipmapGenerator(const wgpu::Device& device) {
    m_device = device;
    initUniformBuffers();
    initBindGroupLayouts();
    initComputePipelines();
    initRenderPipeline();
}

void MipmapGenerator::GenerateMipmaps(const wgpu::Texture& texture, wgpu::Extent3D size,
                                      MipKind kind) {
    switch (kind) {
    case MipKind::LinearUNorm2D:
        generate2DCompute(texture, size, m_pipeline2D, m_bindGroupLayout2D);
        break;
    case MipKind::Normal2D:
        generate2DCompute(texture, size, m_pipelineNormal2D, m_bindGroupLayout2D);
        break;
    case MipKind::Float16Cube:
        generateCubeCompute(texture, size);
        break;
    case MipKind::SRGB2D:
        generate2DRenderSRGB(texture, size);
        break;
    default:
        generate2DCompute(texture, size, m_pipeline2D, m_bindGroupLayout2D);
        break;
    }
}

void MipmapGenerator::initUniformBuffers() {
    wgpu::BufferDescriptor bufferDescriptor{};
    bufferDescriptor.size = sizeof(uint32_t); // Face id
    bufferDescriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;

    for (uint32_t face = 0; face < 6; ++face) {
        m_uniformBuffers[face] = m_device.CreateBuffer(&bufferDescriptor);

        uint32_t faceIndexValue = face;
        m_device.GetQueue().WriteBuffer(m_uniformBuffers[face], 0, &faceIndexValue,
                                        sizeof(uint32_t));
    }
}

void MipmapGenerator::initBindGroupLayouts() {
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
    for (uint32_t face = 0; face < 6; ++face) {
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

void MipmapGenerator::initComputePipelines() {
    std::vector<wgpu::BindGroupLayout> layouts2D = {m_bindGroupLayout2D};
    std::vector<wgpu::BindGroupLayout> layoutsCube = {m_bindGroupLayoutCube, m_bindGroupLayoutFace};
    m_pipeline2D = createComputePipeline("./assets/shaders/mipmap_generator_2d.wgsl", layouts2D);
    m_pipelineCube =
        createComputePipeline("./assets/shaders/mipmap_generator_cube.wgsl", layoutsCube);
    m_pipelineNormal2D =
        createComputePipeline("./assets/shaders/mipmap_generator_normal_2d.wgsl", layouts2D);
}

wgpu::ComputePipeline
MipmapGenerator::createComputePipeline(const std::string& shaderPath,
                                       const std::vector<wgpu::BindGroupLayout>& layouts) {
    std::string shaderCode = LoadShaderFile(shaderPath);

    wgpu::ShaderSourceWGSL wgsl{{.nextInChain = nullptr, .code = shaderCode.c_str()}};
    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{.nextInChain = &wgsl};
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

wgpu::RenderPipeline MipmapGenerator::createRenderPipeline(const std::string& shaderPath,
                                                           wgpu::TextureFormat colorFormat) {
    std::string shaderCode = LoadShaderFile(shaderPath);

    wgpu::ShaderSourceWGSL wgsl{{.nextInChain = nullptr, .code = shaderCode.c_str()}};
    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{.nextInChain = &wgsl};
    wgpu::ShaderModule shaderModule = m_device.CreateShaderModule(&shaderModuleDescriptor);

    // Bind group layout: texture only (using textureLoad, no sampler needed)
    wgpu::BindGroupLayoutEntry entries[1]{};
    entries[0].binding = 0;
    entries[0].visibility = wgpu::ShaderStage::Fragment;
    entries[0].texture.sampleType = wgpu::TextureSampleType::Float;
    entries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
    entries[0].texture.multisampled = false;

    wgpu::BindGroupLayoutDescriptor bglDesc{};
    bglDesc.entryCount = 1;
    bglDesc.entries = entries;
    m_renderBindGroupLayout = m_device.CreateBindGroupLayout(&bglDesc);

    wgpu::BindGroupLayout bindGroupLayouts[] = {m_renderBindGroupLayout};
    wgpu::PipelineLayoutDescriptor layoutDescriptor{};
    layoutDescriptor.bindGroupLayoutCount = 1;
    layoutDescriptor.bindGroupLayouts = bindGroupLayouts;
    wgpu::PipelineLayout pipelineLayout = m_device.CreatePipelineLayout(&layoutDescriptor);

    wgpu::ColorTargetState colorTarget{};
    colorTarget.format = colorFormat;

    wgpu::FragmentState fragmentState{};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = "fs_main";
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    wgpu::RenderPipelineDescriptor desc{};
    desc.layout = pipelineLayout;
    desc.vertex.module = shaderModule;
    desc.vertex.entryPoint = "vs_main";
    desc.vertex.bufferCount = 0;
    desc.vertex.buffers = nullptr;
    desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    desc.fragment = &fragmentState;

    return m_device.CreateRenderPipeline(&desc);
}

void MipmapGenerator::initRenderPipeline() {
    // Create render pipeline targeting sRGB RGBA8 color
    m_renderPipelineSRGB2D = createRenderPipeline("./assets/shaders/mipmap_downsample_render.wgsl",
                                                  m_renderColorFormatSRGB);
}

void MipmapGenerator::generate2DCompute(const wgpu::Texture& texture, wgpu::Extent3D size,
                                        const wgpu::ComputePipeline& pipeline,
                                        const wgpu::BindGroupLayout& layout) {
    uint32_t mipLevelCount =
        1 + static_cast<uint32_t>(std::log2(std::max(size.width, size.height)));

    // Create mip level views
    wgpu::TextureViewDescriptor viewDescriptor{};
    viewDescriptor.format = wgpu::TextureFormat::RGBA8Unorm;
    viewDescriptor.dimension = wgpu::TextureViewDimension::e2D;
    viewDescriptor.baseMipLevel = 0;
    viewDescriptor.mipLevelCount = 1;
    viewDescriptor.baseArrayLayer = 0;
    viewDescriptor.arrayLayerCount = 1;

    std::vector<wgpu::TextureView> mipLevelViews(mipLevelCount);
    for (uint32_t i = 0; i < mipLevelCount; ++i) {
        viewDescriptor.baseMipLevel = i;
        mipLevelViews[i] = texture.CreateView(&viewDescriptor);
    }

    wgpu::CommandEncoder encoder = m_device.CreateCommandEncoder();
    wgpu::ComputePassEncoder computePass = encoder.BeginComputePass();
    computePass.SetPipeline(pipeline);

    wgpu::BindGroupDescriptor bindGroupDescriptor{};
    bindGroupDescriptor.layout = layout;
    bindGroupDescriptor.entryCount = 2;
    wgpu::BindGroupEntry bindGroupEntries[2]{};
    bindGroupEntries[0].binding = 0;
    bindGroupEntries[1].binding = 1;

    for (uint32_t nextLevel = 1; nextLevel < mipLevelViews.size(); ++nextLevel) {
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
    m_device.GetQueue().Submit(1, &commands);
}

void MipmapGenerator::generateCubeCompute(const wgpu::Texture& texture, wgpu::Extent3D size) {
    const uint32_t mipLevelCount =
        1 + static_cast<uint32_t>(std::log2(std::max(size.width, size.height)));

    // Create views per mip level (2D array views over 6 faces)
    wgpu::TextureViewDescriptor viewDescriptor{};
    viewDescriptor.format = wgpu::TextureFormat::RGBA16Float;
    viewDescriptor.dimension = wgpu::TextureViewDimension::e2DArray;
    viewDescriptor.baseMipLevel = 0;
    viewDescriptor.mipLevelCount = 1;
    viewDescriptor.baseArrayLayer = 0;
    viewDescriptor.arrayLayerCount = 6u;

    std::vector<wgpu::TextureView> mipLevelViews(mipLevelCount);
    for (uint32_t i = 0; i < mipLevelCount; ++i) {
        viewDescriptor.baseMipLevel = i;
        mipLevelViews[i] = texture.CreateView(&viewDescriptor);
    }

    // Command encoding
    wgpu::CommandEncoder encoder = m_device.CreateCommandEncoder();
    wgpu::ComputePassEncoder computePass = encoder.BeginComputePass();
    computePass.SetPipeline(m_pipelineCube);

    // Bind group layout for cube path
    wgpu::BindGroupDescriptor bindGroupDescriptor{};
    bindGroupDescriptor.layout = m_bindGroupLayoutCube;
    bindGroupDescriptor.entryCount = 2;
    wgpu::BindGroupEntry bindGroupEntries[2]{};
    bindGroupEntries[0].binding = 0; // Previous mip level
    bindGroupEntries[1].binding = 1; // Next mip level

    // For each face and mip level
    for (uint32_t face = 0; face < 6u; ++face) {
        // Set per-face uniform (group 1)
        computePass.SetBindGroup(1, m_faceBindGroups[face], 0, nullptr);

        for (uint32_t nextLevel = 1; nextLevel < mipLevelViews.size(); ++nextLevel) {
            const uint32_t width = std::max(1u, size.width >> nextLevel);
            const uint32_t height = std::max(1u, size.height >> nextLevel);

            // Bind prev/next level views (group 0)
            bindGroupEntries[0].textureView = mipLevelViews[nextLevel - 1];
            bindGroupEntries[1].textureView = mipLevelViews[nextLevel];
            bindGroupDescriptor.entries = bindGroupEntries;
            wgpu::BindGroup bindGroup = m_device.CreateBindGroup(&bindGroupDescriptor);
            computePass.SetBindGroup(0, bindGroup, 0, nullptr);

            constexpr uint32_t workgroupSize = 8;
            const uint32_t workgroupCountX = (width + workgroupSize - 1) / workgroupSize;
            const uint32_t workgroupCountY = (height + workgroupSize - 1) / workgroupSize;
            computePass.DispatchWorkgroups(workgroupCountX, workgroupCountY, 1);
        }
    }

    computePass.End();
    wgpu::CommandBuffer cb = encoder.Finish();
    m_device.GetQueue().Submit(1, &cb);
}

void MipmapGenerator::generate2DRenderSRGB(const wgpu::Texture& texture, wgpu::Extent3D size) {
    const uint32_t mipLevelCount =
        1 + static_cast<uint32_t>(std::log2(std::max(size.width, size.height)));

    // Create command encoder
    wgpu::CommandEncoder encoder = m_device.CreateCommandEncoder();

    // Iterate over mip levels
    for (uint32_t nextLevel = 1; nextLevel < mipLevelCount; ++nextLevel) {
        // Views for prev (sampled) and next (render target) levels
        wgpu::TextureViewDescriptor prevDesc{};
        prevDesc.format = wgpu::TextureFormat::RGBA8UnormSrgb;
        prevDesc.dimension = wgpu::TextureViewDimension::e2D;
        prevDesc.baseMipLevel = nextLevel - 1;
        prevDesc.mipLevelCount = 1;
        prevDesc.baseArrayLayer = 0;
        prevDesc.arrayLayerCount = 1;
        wgpu::TextureView prevView = texture.CreateView(&prevDesc);

        wgpu::TextureViewDescriptor nextDesc = prevDesc;
        nextDesc.baseMipLevel = nextLevel;
        wgpu::TextureView nextView = texture.CreateView(&nextDesc);

        // Create bind group for prev level (texture only, using textureLoad)
        wgpu::BindGroupEntry entries[1]{};
        entries[0].binding = 0;
        entries[0].textureView = prevView;

        wgpu::BindGroupDescriptor bgd{};
        bgd.layout = m_renderBindGroupLayout;
        bgd.entryCount = 1;
        bgd.entries = entries;
        wgpu::BindGroup bindGroup = m_device.CreateBindGroup(&bgd);

        // Render pass to write next level
        wgpu::RenderPassColorAttachment color{};
        color.view = nextView;
        color.loadOp = wgpu::LoadOp::Clear;
        color.storeOp = wgpu::StoreOp::Store;
        color.clearValue = {0.0f, 0.0f, 0.0f, 0.0f};

        wgpu::RenderPassDescriptor rp{};
        rp.colorAttachmentCount = 1;
        rp.colorAttachments = &color;

        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&rp);
        pass.SetPipeline(m_renderPipelineSRGB2D);
        pass.SetBindGroup(0, bindGroup);
        pass.Draw(3, 1, 0, 0); // Fullscreen triangle
        pass.End();
    }

    // Submit
    wgpu::CommandBuffer cb = encoder.Finish();
    m_device.GetQueue().Submit(1, &cb);
}