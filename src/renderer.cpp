// Standard Library Headers
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// Third-Party Library Headers
#include <GLFW/glfw3.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RIGHT_HANDED
#include <glm/ext.hpp>
#include <glm/glm.hpp>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#else
#include <webgpu/webgpu_glfw.h>
#endif

// Project Headers
#include "application.h"
#include "environment.h"
#include "environment_preprocessor.h"
#include "model.h"
#include "orbit_controls.h"
#include "panorama_to_cubemap_converter.h"
#include "renderer.h"

//----------------------------------------------------------------------
// Internal Utility Functions

namespace
{

constexpr uint32_t kIrradianceMapSize = 64;
constexpr uint32_t kPrecomputedSpecularMapSize = 512;
constexpr uint32_t kBRDFIntegrationLUTMapSize = 128;

int FloorPow2(int x)
{
    int power = 1;
    while (power * 2 <= x)
        power *= 2;
    return power;
}

template <typename TextureInfo>
void CreateTexture(const TextureInfo *textureInfo, wgpu::TextureFormat format, glm::vec4 defaultValue,
                   wgpu::Device device, MipmapGenerator &mipmapGenerator, MipmapGenerator::MipKind kind, wgpu::Texture &texture,
                   wgpu::TextureView &textureView)
{
    // Set default pixel value
    const uint8_t defaultPixel[4] = {
        static_cast<uint8_t>(defaultValue.r * 255.0f), static_cast<uint8_t>(defaultValue.g * 255.0f),
        static_cast<uint8_t>(defaultValue.b * 255.0f), static_cast<uint8_t>(defaultValue.a * 255.0f)};
    const uint8_t *data = defaultPixel;
    uint32_t width = 1;
    uint32_t height = 1;

    if (textureInfo)
    {
        width = textureInfo->m_width;
        height = textureInfo->m_height;
        data = textureInfo->m_data.data();
    }

    // Compute the number of mip levels
    uint32_t mipLevelCount = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

    if (kind == MipmapGenerator::MipKind::SRGB2D)
    {
        // Create final SRGB texture directly with render attachment usage
        wgpu::TextureDescriptor finalDesc{};
        finalDesc.size = {width, height, 1};
        finalDesc.format = format; // expected RGBA8UnormSrgb
        finalDesc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopyDst;
        finalDesc.mipLevelCount = mipLevelCount;
        texture = device.CreateTexture(&finalDesc);

        // Upload level 0
        wgpu::ImageCopyTexture imageCopyTexture{};
        imageCopyTexture.texture = texture;
        imageCopyTexture.mipLevel = 0;
        imageCopyTexture.origin = {0, 0, 0};
        imageCopyTexture.aspect = wgpu::TextureAspect::All;

        wgpu::TextureDataLayout source{};
        source.offset = 0;
        source.bytesPerRow = 4 * width * sizeof(uint8_t);
        source.rowsPerImage = height;

        device.GetQueue().WriteTexture(&imageCopyTexture, data, 4 * width * height * sizeof(uint8_t), &source, &finalDesc.size);

        // Generate mips directly via render path
        mipmapGenerator.GenerateMipmaps(texture, finalDesc.size, kind);
    }
    else
    {
        // Create an intermediate texture for compute-based mip generation (UNORM)
        wgpu::TextureDescriptor textureDescriptor{};
        textureDescriptor.size = {width, height, 1};
        textureDescriptor.format = wgpu::TextureFormat::RGBA8Unorm;
        textureDescriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::StorageBinding |
                                  wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::CopySrc;
        textureDescriptor.mipLevelCount = mipLevelCount;

        wgpu::Texture intermediateTexture = device.CreateTexture(&textureDescriptor);

        // Upload the texture data to intermediate
        wgpu::ImageCopyTexture imageCopyTexture{};
        imageCopyTexture.texture = intermediateTexture;
        imageCopyTexture.mipLevel = 0;
        imageCopyTexture.origin = {0, 0, 0};
        imageCopyTexture.aspect = wgpu::TextureAspect::All;

        wgpu::TextureDataLayout source;
        source.offset = 0;
        source.bytesPerRow = 4 * width * sizeof(uint8_t);
        source.rowsPerImage = height;

        device.GetQueue().WriteTexture(&imageCopyTexture, data, 4 * width * height * sizeof(uint8_t), &source,
                                       &textureDescriptor.size);

        // Generate mipmaps via compute (normal-aware or linear depending on kind)
        mipmapGenerator.GenerateMipmaps(intermediateTexture, textureDescriptor.size, kind == MipmapGenerator::MipKind::Normal2D ? MipmapGenerator::MipKind::Normal2D : MipmapGenerator::MipKind::LinearUNorm2D);

        // Create the final texture (may be sRGB or UNORM depending on input format)
        textureDescriptor.format = format;
        textureDescriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        wgpu::Texture finalTexture = device.CreateTexture(&textureDescriptor);

        // Copy the intermediate texture to the final texture
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();

        for (uint32_t level = 0; level < mipLevelCount; ++level)
        {
            uint32_t mipWidth = std::max(width >> level, 1u);
            uint32_t mipHeight = std::max(height >> level, 1u);
            wgpu::ImageCopyTexture src{};
            src.texture = intermediateTexture;
            src.mipLevel = level;
            src.origin = {0, 0, 0};
            src.aspect = wgpu::TextureAspect::All;
            wgpu::ImageCopyTexture dst{};
            dst.texture = finalTexture;
            dst.mipLevel = level;
            dst.origin = {0, 0, 0};
            dst.aspect = wgpu::TextureAspect::All;
            wgpu::Extent3D extent = {mipWidth, mipHeight, 1};
            encoder.CopyTextureToTexture(&src, &dst, &extent);
        }

        wgpu::CommandBuffer commandBuffer = encoder.Finish();
        device.GetQueue().Submit(1, &commandBuffer);

        texture = finalTexture;
    }

    // Create a texture view covering all mip levels
    wgpu::TextureViewDescriptor viewDescriptor{};
    viewDescriptor.format = format;
    viewDescriptor.dimension = wgpu::TextureViewDimension::e2D;
    viewDescriptor.mipLevelCount = mipLevelCount;
    viewDescriptor.arrayLayerCount = 1;

    textureView = texture.CreateView(&viewDescriptor);
}

void CreateEnvironmentTexture(wgpu::Device device, wgpu::TextureViewDimension type, wgpu::Extent3D size,
                              bool mipmapping, wgpu::Texture &texture, wgpu::TextureView &textureView)
{
    // Compute the number of mip levels
    const uint32_t mipLevelCount =
        mipmapping ? static_cast<uint32_t>(std::log2(std::max(size.width, size.height))) + 1 : 1;

    // Create a WebGPU texture descriptor with mipmapping enabled
    wgpu::TextureDescriptor textureDescriptor{};
    textureDescriptor.size = size;
    textureDescriptor.format = wgpu::TextureFormat::RGBA16Float;
    textureDescriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::StorageBinding |
                              wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::CopySrc;
    textureDescriptor.mipLevelCount = mipLevelCount;

    texture = device.CreateTexture(&textureDescriptor);

    // Create a texture view covering all mip levels
    wgpu::TextureViewDescriptor viewDescriptor{};
    viewDescriptor.format = wgpu::TextureFormat::RGBA16Float;
    viewDescriptor.dimension = type;
    viewDescriptor.mipLevelCount = mipLevelCount;
    viewDescriptor.arrayLayerCount = size.depthOrArrayLayers;

    textureView = texture.CreateView(&viewDescriptor);
}

} // namespace

//----------------------------------------------------------------------
// Renderer Class implementation

void Renderer::Initialize(GLFWwindow *window, const Environment &environment, const Model &model, uint32_t width,
                          uint32_t height, const std::function<void()> &callback)
{
    m_instance = wgpu::CreateInstance();
    // Capture pointers to avoid dangling references across async callbacks
    const Environment *envPtr = &environment;
    const Model *modelPtr = &model;
    GLFWwindow *windowPtr = window;

    GetAdapter([this, callback, envPtr, modelPtr, width, height, windowPtr](wgpu::Adapter adapter) {
        m_adapter = adapter;
        GetDevice([this, callback, envPtr, modelPtr, width, height, windowPtr](wgpu::Device device) {
            m_device = device;

#if defined(__EMSCRIPTEN__)
            (void)windowPtr; // Unused parameter
            wgpu::SurfaceDescriptorFromCanvasHTMLSelector canvasDesc{};
            canvasDesc.selector = "#canvas";
            wgpu::SurfaceDescriptor surfaceDesc{};
            surfaceDesc.nextInChain = &canvasDesc;
            m_surface = m_instance.CreateSurface(&surfaceDesc);
#else
            m_surface = wgpu::glfw::CreateSurfaceForWindow(m_instance, windowPtr);
#endif

            InitGraphics(*envPtr, *modelPtr, width, height);

            // Return control to the application
            callback();
        });
    });
}

void Renderer::Resize(uint32_t width, uint32_t height)
{
    // Recreate the depth texture
    CreateDepthTexture(width, height);

    // Update the surface configuration
    ConfigureSurface(width, height);

    // Refresh depth attachment view
    m_depthAttachment.view = m_depthTextureView;
}

void Renderer::Render(const glm::mat4 &modelMatrix, const CameraUniformsInput &camera)
{
    // Update view dependent data
    UpdateUniforms(modelMatrix, camera);
    SortTransparentMeshes(modelMatrix, camera.viewMatrix);

    // Ge the current surface texture and update the color attachment view
    wgpu::SurfaceTexture surfaceTexture;
    m_surface.GetCurrentTexture(&surfaceTexture);
    if (!surfaceTexture.texture)
    {
        std::cerr << "Error: Failed to get current surface texture." << std::endl;
        return;
    }
    m_colorAttachment.view = surfaceTexture.texture.CreateView();

    // Create command encoder and render pass
    wgpu::CommandEncoder encoder = m_device.CreateCommandEncoder();
    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&m_renderPassDescriptor);

    // Set global bind group (group 0)
    pass.SetBindGroup(0, m_globalBindGroup);

    // Render environment background first
    pass.SetPipeline(m_environmentPipeline);
    pass.Draw(3, 1, 0, 0); // Fullscreen triangle

    // Set up vertex and index buffers
    pass.SetVertexBuffer(0, m_vertexBuffer);
    pass.SetIndexBuffer(m_indexBuffer, wgpu::IndexFormat::Uint32);

    // Draw opaque submeshes
    pass.SetPipeline(m_modelPipelineOpaque);
    for (auto subMesh : m_opaqueMeshes)
    {
        pass.SetBindGroup(1, m_materials[subMesh.m_materialIndex].m_bindGroup);
        pass.DrawIndexed(subMesh.m_indexCount, 1u, subMesh.m_firstIndex);
    }

    // Draw transparent submeshes back-to-front
    pass.SetPipeline(m_modelPipelineTransparent);
    for (auto depthInfo : m_transparentMeshesDepthSorted)
    {
        const SubMesh &subMesh = m_transparentMeshes[depthInfo.m_meshIndex];
        pass.SetBindGroup(1, m_materials[subMesh.m_materialIndex].m_bindGroup);
        pass.DrawIndexed(subMesh.m_indexCount, 1u, subMesh.m_firstIndex);
    }

    // End the pass
    pass.End();

    // Submit commands
    wgpu::CommandBuffer commands = encoder.Finish();
    m_device.GetQueue().Submit(1, &commands);

    // Present the surface
#if !defined(__EMSCRIPTEN__)
    m_surface.Present();
    m_instance.ProcessEvents();
#endif
}

void Renderer::ReloadShaders()
{
    m_environmentPipeline = nullptr;
    m_environmentShaderModule = nullptr;
    m_modelPipelineOpaque = nullptr;
    m_modelPipelineTransparent = nullptr;
    m_modelShaderModule = nullptr;

    CreateEnvironmentRenderPipeline();
    CreateModelRenderPipelines();
}

void Renderer::UpdateModel(const Model &model)
{
    auto t0 = std::chrono::high_resolution_clock::now();

    // Destroy the existing model resources
    m_vertexBuffer = nullptr;
    m_indexBuffer = nullptr;

    // Create new model resources
    CreateVertexBuffer(model);
    CreateIndexBuffer(model);
    CreateSubMeshes(model);
    CreateMaterials(model);

    auto t1 = std::chrono::high_resolution_clock::now();
    double totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "Updated Model WebGPU resources in " << totalMs << "ms" << std::endl;
}

void Renderer::UpdateEnvironment(const Environment &environment)
{
    auto t0 = std::chrono::high_resolution_clock::now();

    // Destroy the existing environment resources
    m_environmentTexture = nullptr;
    m_environmentTextureView = nullptr;
    m_iblIrradianceTexture = nullptr;
    m_iblIrradianceTextureView = nullptr;
    m_iblSpecularTexture = nullptr;
    m_iblSpecularTextureView = nullptr;
    m_iblBrdfIntegrationLUT = nullptr;
    m_iblBrdfIntegrationLUTView = nullptr;

    // Create new environment resources
    CreateEnvironmentTextures(environment);
    CreateGlobalBindGroup();

    auto t1 = std::chrono::high_resolution_clock::now();
    double totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::cout << "Updated Environment WebGPU resources in " << totalMs << "ms" << std::endl;
}

void Renderer::InitGraphics(const Environment &environment, const Model &model, uint32_t width, uint32_t height)
{
    ConfigureSurface(width, height);
    CreateDepthTexture(width, height);

    CreateBindGroupLayouts();

    CreateSamplers();

    CreateRenderPassDescriptor();

    m_mipmapGenerator = std::make_unique<MipmapGenerator>(m_device);
    CreateDefaultTextures();

    CreateModelRenderPipelines();
    CreateEnvironmentRenderPipeline();

    CreateUniformBuffers();

    UpdateEnvironment(environment);

    UpdateModel(model);
}

void Renderer::CreateDefaultTextures()
{
    // 1x1 white sRGB texture
    {
        const uint8_t whitePixel[4] = {255, 255, 255, 255};
        wgpu::TextureDescriptor desc{};
        desc.size = {1, 1, 1};
        desc.format = wgpu::TextureFormat::RGBA8UnormSrgb;
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        m_defaultSRGBTexture = m_device.CreateTexture(&desc);

        wgpu::ImageCopyTexture dst{};
        dst.texture = m_defaultSRGBTexture;
        wgpu::TextureDataLayout layout{};
        layout.bytesPerRow = 4;
        wgpu::Extent3D size{1, 1, 1};
        m_device.GetQueue().WriteTexture(&dst, whitePixel, sizeof(whitePixel), &layout, &size);

        m_defaultSRGBTextureView = m_defaultSRGBTexture.CreateView();
    }

    // 1x1 white UNORM texture
    {
        const uint8_t whitePixel[4] = {255, 255, 255, 255};
        wgpu::TextureDescriptor desc{};
        desc.size = {1, 1, 1};
        desc.format = wgpu::TextureFormat::RGBA8Unorm;
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        m_defaultUNormTexture = m_device.CreateTexture(&desc);

        wgpu::ImageCopyTexture dst{};
        dst.texture = m_defaultUNormTexture;
        wgpu::TextureDataLayout layout{};
        layout.bytesPerRow = 4;
        wgpu::Extent3D size{1, 1, 1};
        m_device.GetQueue().WriteTexture(&dst, whitePixel, sizeof(whitePixel), &layout, &size);

        m_defaultUNormTextureView = m_defaultUNormTexture.CreateView();
    }

    // 1x1 flat normal (128,128,255,255) UNORM
    {
        const uint8_t flatNormal[4] = {128, 128, 255, 255};
        wgpu::TextureDescriptor desc{};
        desc.size = {1, 1, 1};
        desc.format = wgpu::TextureFormat::RGBA8Unorm;
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        m_defaultNormalTexture = m_device.CreateTexture(&desc);

        wgpu::ImageCopyTexture dst{};
        dst.texture = m_defaultNormalTexture;
        wgpu::TextureDataLayout layout{};
        layout.bytesPerRow = 4;
        wgpu::Extent3D size{1, 1, 1};
        m_device.GetQueue().WriteTexture(&dst, flatNormal, sizeof(flatNormal), &layout, &size);

        m_defaultNormalTextureView = m_defaultNormalTexture.CreateView();
    }
}
void Renderer::ConfigureSurface(uint32_t width, uint32_t height)
{
    wgpu::SurfaceCapabilities capabilities;
    m_surface.GetCapabilities(m_adapter, &capabilities);
    m_surfaceFormat = capabilities.formats[0];
    wgpu::SurfaceConfiguration config{};
    config.device = m_device;
    config.format = m_surfaceFormat;
    config.width = width;
    config.height = height;
    m_surface.Configure(&config);
}

void Renderer::CreateDepthTexture(uint32_t width, uint32_t height)
{
    wgpu::TextureDescriptor depthTextureDescriptor{};
    depthTextureDescriptor.size = {width, height, 1};
    depthTextureDescriptor.format = wgpu::TextureFormat::Depth24PlusStencil8;
    depthTextureDescriptor.usage = wgpu::TextureUsage::RenderAttachment;

    m_depthTexture = m_device.CreateTexture(&depthTextureDescriptor);
    m_depthTextureView = m_depthTexture.CreateView();
}

void Renderer::CreateBindGroupLayouts()
{
    wgpu::BindGroupLayoutEntry globalLayoutEntries[7]{};

    // 0: Global uniforms
    globalLayoutEntries[0].binding = 0;
    globalLayoutEntries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    globalLayoutEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    globalLayoutEntries[0].buffer.hasDynamicOffset = false;
    globalLayoutEntries[0].buffer.minBindingSize = sizeof(GlobalUniforms);

    // 1: Sampler binding
    globalLayoutEntries[1].binding = 1;
    globalLayoutEntries[1].visibility = wgpu::ShaderStage::Fragment;
    globalLayoutEntries[1].sampler.type = wgpu::SamplerBindingType::Filtering;

    // 2: Environment texture binding
    globalLayoutEntries[2].binding = 2;
    globalLayoutEntries[2].visibility = wgpu::ShaderStage::Fragment;
    globalLayoutEntries[2].texture.sampleType = wgpu::TextureSampleType::Float;
    globalLayoutEntries[2].texture.viewDimension = wgpu::TextureViewDimension::Cube;
    globalLayoutEntries[2].texture.multisampled = false;

    // 3: IBL irradiance texture binding
    globalLayoutEntries[3].binding = 3;
    globalLayoutEntries[3].visibility = wgpu::ShaderStage::Fragment;
    globalLayoutEntries[3].texture.sampleType = wgpu::TextureSampleType::Float;
    globalLayoutEntries[3].texture.viewDimension = wgpu::TextureViewDimension::Cube;
    globalLayoutEntries[3].texture.multisampled = false;

    // 4: IBL specular texture binding
    globalLayoutEntries[4].binding = 4;
    globalLayoutEntries[4].visibility = wgpu::ShaderStage::Fragment;
    globalLayoutEntries[4].texture.sampleType = wgpu::TextureSampleType::Float;
    globalLayoutEntries[4].texture.viewDimension = wgpu::TextureViewDimension::Cube;
    globalLayoutEntries[4].texture.multisampled = false;

    // 5: IBL LUT texture binding
    globalLayoutEntries[5].binding = 5;
    globalLayoutEntries[5].visibility = wgpu::ShaderStage::Fragment;
    globalLayoutEntries[5].texture.sampleType = wgpu::TextureSampleType::Float;
    globalLayoutEntries[5].texture.viewDimension = wgpu::TextureViewDimension::e2D;
    globalLayoutEntries[5].texture.multisampled = false;

    // 6: IBL LUT sampler binding
    globalLayoutEntries[6].binding = 6;
    globalLayoutEntries[6].visibility = wgpu::ShaderStage::Fragment;
    globalLayoutEntries[6].sampler.type = wgpu::SamplerBindingType::Filtering;

    wgpu::BindGroupLayoutDescriptor globalBindGroupLayoutDescriptor{};
    globalBindGroupLayoutDescriptor.entryCount = 7;
    globalBindGroupLayoutDescriptor.entries = globalLayoutEntries;

    m_globalBindGroupLayout = m_device.CreateBindGroupLayout(&globalBindGroupLayoutDescriptor);

    wgpu::BindGroupLayoutEntry modelLayoutEntries[8]{};

    // 0: Model uniforms
    modelLayoutEntries[0].binding = 0;
    modelLayoutEntries[0].visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    modelLayoutEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    modelLayoutEntries[0].buffer.hasDynamicOffset = false;
    modelLayoutEntries[0].buffer.minBindingSize = sizeof(ModelUniforms);

    // 1: Material uniforms
    modelLayoutEntries[1].binding = 1;
    modelLayoutEntries[1].visibility = wgpu::ShaderStage::Fragment;
    modelLayoutEntries[1].buffer.type = wgpu::BufferBindingType::Uniform;
    modelLayoutEntries[1].buffer.hasDynamicOffset = false;
    modelLayoutEntries[1].buffer.minBindingSize = sizeof(MaterialUniforms);

    // 2: Sampler binding
    modelLayoutEntries[2].binding = 2;
    modelLayoutEntries[2].visibility = wgpu::ShaderStage::Fragment;
    modelLayoutEntries[2].sampler.type = wgpu::SamplerBindingType::Filtering;

    // 3..7 textures
    for (int t = 0; t < 5; ++t)
    {
        const uint32_t binding = 3 + t;
        modelLayoutEntries[binding].binding = binding;
        modelLayoutEntries[binding].visibility = wgpu::ShaderStage::Fragment;
        modelLayoutEntries[binding].texture.sampleType = wgpu::TextureSampleType::Float;
        modelLayoutEntries[binding].texture.viewDimension = wgpu::TextureViewDimension::e2D;
        modelLayoutEntries[binding].texture.multisampled = false;
    }

    wgpu::BindGroupLayoutDescriptor modelBindGroupLayoutDescriptor{};
    modelBindGroupLayoutDescriptor.entryCount = 8;
    modelBindGroupLayoutDescriptor.entries = modelLayoutEntries;

    m_modelBindGroupLayout = m_device.CreateBindGroupLayout(&modelBindGroupLayoutDescriptor);
}

void Renderer::CreateSamplers()
{
    // Model textures sampler
    if (!m_sampler)
    {
        wgpu::SamplerDescriptor samplerDescriptor{};
        samplerDescriptor.addressModeU = wgpu::AddressMode::Repeat;
        samplerDescriptor.addressModeV = wgpu::AddressMode::Repeat;
        samplerDescriptor.addressModeW = wgpu::AddressMode::Repeat;
        samplerDescriptor.minFilter = wgpu::FilterMode::Linear;
        samplerDescriptor.magFilter = wgpu::FilterMode::Linear;
        samplerDescriptor.mipmapFilter = wgpu::MipmapFilterMode::Linear;
        m_sampler = m_device.CreateSampler(&samplerDescriptor);
    }

    // Environment cube sampler
    if (!m_environmentCubeSampler)
    {
        wgpu::SamplerDescriptor samplerDescriptor{};
        samplerDescriptor.addressModeU = wgpu::AddressMode::Repeat;
        samplerDescriptor.addressModeV = wgpu::AddressMode::Repeat;
        samplerDescriptor.addressModeW = wgpu::AddressMode::Repeat;
        samplerDescriptor.minFilter = wgpu::FilterMode::Linear;
        samplerDescriptor.magFilter = wgpu::FilterMode::Linear;
        samplerDescriptor.mipmapFilter = wgpu::MipmapFilterMode::Linear;
        m_environmentCubeSampler = m_device.CreateSampler(&samplerDescriptor);
    }

    // BRDF LUT sampler
    if (!m_iblBrdfIntegrationLUTSampler)
    {
        wgpu::SamplerDescriptor samplerDescriptor{};
        samplerDescriptor.addressModeU = wgpu::AddressMode::ClampToEdge;
        samplerDescriptor.addressModeV = wgpu::AddressMode::ClampToEdge;
        samplerDescriptor.addressModeW = wgpu::AddressMode::ClampToEdge;
        samplerDescriptor.minFilter = wgpu::FilterMode::Linear;
        samplerDescriptor.magFilter = wgpu::FilterMode::Linear;
        samplerDescriptor.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
        m_iblBrdfIntegrationLUTSampler = m_device.CreateSampler(&samplerDescriptor);
    }
}

void Renderer::CreateRenderPassDescriptor()
{
    // Configure color attachment
    m_colorAttachment.loadOp = wgpu::LoadOp::Clear;
    m_colorAttachment.storeOp = wgpu::StoreOp::Store;
    m_colorAttachment.clearValue = {.r = 0.0f, .g = 0.2f, .b = 0.4f, .a = 1.0f};

    // Configure depth attachment
    m_depthAttachment.view = m_depthTextureView;
    m_depthAttachment.depthLoadOp = wgpu::LoadOp::Clear;
    m_depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
    m_depthAttachment.depthClearValue = 1.0f;
    m_depthAttachment.stencilLoadOp = wgpu::LoadOp::Clear;
    m_depthAttachment.stencilStoreOp = wgpu::StoreOp::Store;

    // Initialize render pass descriptor
    m_renderPassDescriptor.colorAttachmentCount = 1;
    m_renderPassDescriptor.colorAttachments = &m_colorAttachment;
    m_renderPassDescriptor.depthStencilAttachment = &m_depthAttachment;
}

void Renderer::CreateVertexBuffer(const Model &model)
{
    const std::vector<Model::Vertex> &vertexData = model.GetVertices();

    wgpu::BufferDescriptor vertexBufferDesc{};
    vertexBufferDesc.size = vertexData.size() * sizeof(Model::Vertex);
    vertexBufferDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
    vertexBufferDesc.mappedAtCreation = true;

    m_vertexBuffer = m_device.CreateBuffer(&vertexBufferDesc);
    std::memcpy(m_vertexBuffer.GetMappedRange(), vertexData.data(), vertexData.size() * sizeof(Model::Vertex));
    m_vertexBuffer.Unmap();
}

void Renderer::CreateIndexBuffer(const Model &model)
{
    const std::vector<uint32_t> &indexData = model.GetIndices();

    wgpu::BufferDescriptor indexBufferDesc{};
    indexBufferDesc.size = indexData.size() * sizeof(uint32_t);
    indexBufferDesc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
    indexBufferDesc.mappedAtCreation = true;

    m_indexBuffer = m_device.CreateBuffer(&indexBufferDesc);
    std::memcpy(m_indexBuffer.GetMappedRange(), indexData.data(), indexData.size() * sizeof(uint32_t));
    m_indexBuffer.Unmap();
}

void Renderer::CreateUniformBuffers()
{
    wgpu::BufferDescriptor bufferDescriptor{};
    bufferDescriptor.size = sizeof(GlobalUniforms);
    bufferDescriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;

    m_globalUniformBuffer = m_device.CreateBuffer(&bufferDescriptor);

    // Initialize Global Uniforms with default values
    GlobalUniforms globalUniforms;
    globalUniforms.viewMatrix = glm::mat4(1.0f);              // Initialize as identity
    globalUniforms.projectionMatrix = glm::mat4(1.0f);        // Initialize as identity
    globalUniforms.inverseViewMatrix = glm::mat4(1.0f);       // Initialize as identity
    globalUniforms.inverseProjectionMatrix = glm::mat4(1.0f); // Initialize as identity
    globalUniforms.cameraPosition = glm::vec3(0.0f, 0.0f, 0.0f);

    m_device.GetQueue().WriteBuffer(m_globalUniformBuffer, 0, &globalUniforms, sizeof(GlobalUniforms));

    // Create the model uniform buffer
    bufferDescriptor.size = sizeof(ModelUniforms);
    m_modelUniformBuffer = m_device.CreateBuffer(&bufferDescriptor);

    // Initialize Model Uniforms with default values
    ModelUniforms modelUniforms;
    modelUniforms.modelMatrix = glm::mat4(1.0f);  // Initialize as identity
    modelUniforms.normalMatrix = glm::mat4(1.0f); // Initialize as identity

    m_device.GetQueue().WriteBuffer(m_modelUniformBuffer, 0, &modelUniforms, sizeof(ModelUniforms));
}

void Renderer::CreateEnvironmentTextures(const Environment &environment)
{
    const Environment::Texture &panoramaTexture = environment.GetTexture();
    uint32_t environmentCubeSize = FloorPow2(panoramaTexture.m_width);

    // Create IBL textures
    CreateEnvironmentTexture(m_device, wgpu::TextureViewDimension::Cube, {environmentCubeSize, environmentCubeSize, 6},
                             true, m_environmentTexture, m_environmentTextureView);
    CreateEnvironmentTexture(m_device, wgpu::TextureViewDimension::Cube, {kIrradianceMapSize, kIrradianceMapSize, 6},
                             true, m_iblIrradianceTexture, m_iblIrradianceTextureView);
    CreateEnvironmentTexture(m_device, wgpu::TextureViewDimension::Cube,
                             {kPrecomputedSpecularMapSize, kPrecomputedSpecularMapSize, 6}, true, m_iblSpecularTexture,
                             m_iblSpecularTextureView);
    CreateEnvironmentTexture(m_device, wgpu::TextureViewDimension::e2D,
                             {kBRDFIntegrationLUTMapSize, kBRDFIntegrationLUTMapSize, 1}, false,
                             m_iblBrdfIntegrationLUT, m_iblBrdfIntegrationLUTView);

    // Upload panorama texture and resample to cubemap
    PanoramaToCubemapConverter panoramaToCubemapConverter(m_device);
    panoramaToCubemapConverter.UploadAndConvert(panoramaTexture, m_environmentTexture);
    m_mipmapGenerator->GenerateMipmaps(m_environmentTexture, {environmentCubeSize, environmentCubeSize, 6},
                                       MipmapGenerator::MipKind::Float16Cube);

    // Precompute IBL maps
    EnvironmentPreprocessor environmentPreprocessor(m_device);
    environmentPreprocessor.GenerateMaps(m_environmentTexture, m_iblIrradianceTexture, m_iblSpecularTexture,
                                         m_iblBrdfIntegrationLUT);

    m_mipmapGenerator->GenerateMipmaps(m_iblIrradianceTexture, {kIrradianceMapSize, kIrradianceMapSize, 6},
                                       MipmapGenerator::MipKind::Float16Cube);
}

void Renderer::CreateSubMeshes(const Model &model)
{
    m_opaqueMeshes.clear();
    m_transparentMeshes.clear();
    m_opaqueMeshes.reserve(model.GetSubMeshes().size());

    for (auto srcSubMesh : model.GetSubMeshes())
    {
        SubMesh dstSubMesh = {.m_firstIndex = srcSubMesh.m_firstIndex,
                              .m_indexCount = srcSubMesh.m_indexCount,
                              .m_materialIndex = srcSubMesh.m_materialIndex,
                              .m_centroid = (srcSubMesh.m_minBounds + srcSubMesh.m_maxBounds) * 0.5f};
        if (model.GetMaterials()[srcSubMesh.m_materialIndex].m_alphaMode == Model::AlphaMode::Blend)
        {
            m_transparentMeshes.push_back(dstSubMesh);
        }
        else
        {
            m_opaqueMeshes.push_back(dstSubMesh);
        }
    }
}

void Renderer::CreateMaterials(const Model &model)
{
    m_materials.clear();

    // Check if the model has any textures
    if (!model.GetMaterials().empty())
    {
        m_materials.resize(model.GetMaterials().size());

        for (size_t i = 0; i < model.GetMaterials().size(); ++i)
        {
            const Model::Material &srcMat = model.GetMaterials()[i];
            Material &dstMat = m_materials[i];

            // Create uniform buffer
            wgpu::BufferDescriptor bufferDescriptor{};
            bufferDescriptor.size = sizeof(MaterialUniforms);
            bufferDescriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
            dstMat.m_uniformBuffer = m_device.CreateBuffer(&bufferDescriptor);

            // Initialize Material Uniforms
            dstMat.m_uniforms.baseColorFactor = srcMat.m_baseColorFactor;
            dstMat.m_uniforms.emissiveFactor = srcMat.m_emissiveFactor;
            dstMat.m_uniforms.metallicFactor = srcMat.m_metallicFactor;
            dstMat.m_uniforms.roughnessFactor = srcMat.m_roughnessFactor;
            dstMat.m_uniforms.normalScale = srcMat.m_normalScale;
            dstMat.m_uniforms.occlusionStrength = srcMat.m_occlusionStrength;
            dstMat.m_uniforms.alphaCutoff = srcMat.m_alphaCutoff;
            dstMat.m_uniforms.alphaMode = int(srcMat.m_alphaMode);

            m_device.GetQueue().WriteBuffer(dstMat.m_uniformBuffer, 0, &dstMat.m_uniforms, sizeof(MaterialUniforms));

            // Base Color Texture
            if (const Model::Texture* t = model.GetTexture(srcMat.m_baseColorTexture))
            {
                glm::vec4 defaultBaseColor(1.0f);
                CreateTexture(t, wgpu::TextureFormat::RGBA8UnormSrgb, defaultBaseColor, m_device, *m_mipmapGenerator,
                              MipmapGenerator::MipKind::SRGB2D, dstMat.m_baseColorTexture, dstMat.m_baseColorTextureView);
            }
            else
            {
                dstMat.m_baseColorTextureView = m_defaultSRGBTextureView;
            }

            // Metallic-Roughness
            if (const Model::Texture* t = model.GetTexture(srcMat.m_metallicRoughnessTexture))
            {
                glm::vec4 defaultMR(1.0f);
                CreateTexture(t, wgpu::TextureFormat::RGBA8Unorm, defaultMR, m_device, *m_mipmapGenerator,
                              MipmapGenerator::MipKind::LinearUNorm2D, dstMat.m_metallicRoughnessTexture, dstMat.m_metallicRoughnessTextureView);
            }
            else
            {
                dstMat.m_metallicRoughnessTextureView = m_defaultUNormTextureView;
            }

            // Normal Texture
            if (const Model::Texture* t = model.GetTexture(srcMat.m_normalTexture))
            {
                glm::vec4 defaultNormal(0.5f, 0.5f, 1.0f, 1.0f);
                CreateTexture(t, wgpu::TextureFormat::RGBA8Unorm, defaultNormal, m_device, *m_mipmapGenerator,
                              MipmapGenerator::MipKind::Normal2D, dstMat.m_normalTexture, dstMat.m_normalTextureView);
            }
            else
            {
                dstMat.m_normalTextureView = m_defaultNormalTextureView;
            }

            // Occlusion Texture
            if (const Model::Texture* t = model.GetTexture(srcMat.m_occlusionTexture))
            {
                glm::vec4 defaultOcc(1.0f);
                CreateTexture(t, wgpu::TextureFormat::RGBA8Unorm, defaultOcc, m_device, *m_mipmapGenerator,
                              MipmapGenerator::MipKind::LinearUNorm2D, dstMat.m_occlusionTexture, dstMat.m_occlusionTextureView);
            }
            else
            {
                dstMat.m_occlusionTextureView = m_defaultUNormTextureView;
            }

            // Emissive Texture
            if (const Model::Texture* t = model.GetTexture(srcMat.m_emissiveTexture))
            {
                glm::vec4 defaultEmissive(1.0f);
                CreateTexture(t, wgpu::TextureFormat::RGBA8UnormSrgb, defaultEmissive, m_device, *m_mipmapGenerator,
                              MipmapGenerator::MipKind::SRGB2D, dstMat.m_emissiveTexture, dstMat.m_emissiveTextureView);
            }
            else
            {
                dstMat.m_emissiveTextureView = m_defaultSRGBTextureView;
            }

            // Create bind group
            wgpu::BindGroupEntry bindGroupEntries[8]{};
            bindGroupEntries[0].binding = 0;
            bindGroupEntries[0].buffer = m_modelUniformBuffer;
            bindGroupEntries[0].offset = 0;
            bindGroupEntries[0].size = sizeof(ModelUniforms);

            bindGroupEntries[1].binding = 1;
            bindGroupEntries[1].buffer = dstMat.m_uniformBuffer;
            bindGroupEntries[1].offset = 0;
            bindGroupEntries[1].size = sizeof(MaterialUniforms);

            bindGroupEntries[2].binding = 2;
            bindGroupEntries[2].sampler = m_sampler;

            bindGroupEntries[3].binding = 3;
            bindGroupEntries[3].textureView = dstMat.m_baseColorTextureView;

            bindGroupEntries[4].binding = 4;
            bindGroupEntries[4].textureView = dstMat.m_metallicRoughnessTextureView;

            bindGroupEntries[5].binding = 5;
            bindGroupEntries[5].textureView = dstMat.m_normalTextureView;

            bindGroupEntries[6].binding = 6;
            bindGroupEntries[6].textureView = dstMat.m_occlusionTextureView;

            bindGroupEntries[7].binding = 7;
            bindGroupEntries[7].textureView = dstMat.m_emissiveTextureView;

            wgpu::BindGroupDescriptor bindGroupDescriptor{};
            bindGroupDescriptor.layout = m_modelBindGroupLayout;
            bindGroupDescriptor.entryCount = 8;
            bindGroupDescriptor.entries = bindGroupEntries;

            dstMat.m_bindGroup = m_device.CreateBindGroup(&bindGroupDescriptor);
        }
    }
}

void Renderer::CreateGlobalBindGroup()
{
    wgpu::BindGroupEntry bindGroupEntries[7]{};
    bindGroupEntries[0].binding = 0;
    bindGroupEntries[0].buffer = m_globalUniformBuffer;
    bindGroupEntries[0].offset = 0;
    bindGroupEntries[0].size = sizeof(GlobalUniforms);

    bindGroupEntries[1].binding = 1;
    bindGroupEntries[1].sampler = m_environmentCubeSampler;

    bindGroupEntries[2].binding = 2;
    bindGroupEntries[2].textureView = m_environmentTextureView;

    bindGroupEntries[3].binding = 3;
    bindGroupEntries[3].textureView = m_iblIrradianceTextureView;

    bindGroupEntries[4].binding = 4;
    bindGroupEntries[4].textureView = m_iblSpecularTextureView;

    bindGroupEntries[5].binding = 5;
    bindGroupEntries[5].textureView = m_iblBrdfIntegrationLUTView;

    bindGroupEntries[6].binding = 6;
    bindGroupEntries[6].sampler = m_iblBrdfIntegrationLUTSampler;

    wgpu::BindGroupDescriptor bindGroupDescriptor{};
    bindGroupDescriptor.layout = m_globalBindGroupLayout;
    bindGroupDescriptor.entryCount = 7;
    bindGroupDescriptor.entries = bindGroupEntries;

    m_globalBindGroup = m_device.CreateBindGroup(&bindGroupDescriptor);
}

void Renderer::CreateModelRenderPipelines()
{
    wgpu::ShaderModuleWGSLDescriptor wgslDesc{};
    const std::string shader = LoadShaderFile("./assets/shaders/gltf_pbr.wgsl");
    wgslDesc.code = shader.c_str();

    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{};
    shaderModuleDescriptor.nextInChain = &wgslDesc;
    m_modelShaderModule = m_device.CreateShaderModule(&shaderModuleDescriptor);

    wgpu::VertexAttribute vertexAttributes[] = {
        {.format = wgpu::VertexFormat::Float32x3, .offset = offsetof(Model::Vertex, m_position), .shaderLocation = 0},
        {.format = wgpu::VertexFormat::Float32x3, .offset = offsetof(Model::Vertex, m_normal), .shaderLocation = 1},
        {.format = wgpu::VertexFormat::Float32x4, .offset = offsetof(Model::Vertex, m_tangent), .shaderLocation = 2},
        {.format = wgpu::VertexFormat::Float32x2, .offset = offsetof(Model::Vertex, m_texCoord0), .shaderLocation = 3},
        {.format = wgpu::VertexFormat::Float32x2, .offset = offsetof(Model::Vertex, m_texCoord1), .shaderLocation = 4},
        {.format = wgpu::VertexFormat::Float32x4, .offset = offsetof(Model::Vertex, m_color), .shaderLocation = 5},
    };

    wgpu::VertexBufferLayout vertexBufferLayout{};
    vertexBufferLayout.arrayStride = sizeof(Model::Vertex);
    vertexBufferLayout.stepMode = wgpu::VertexStepMode::Vertex;
    vertexBufferLayout.attributeCount = 6;
    vertexBufferLayout.attributes = vertexAttributes;

    wgpu::ColorTargetState colorTargetState{};
    colorTargetState.format = m_surfaceFormat;

    wgpu::FragmentState fragmentState{};
    fragmentState.module = m_modelShaderModule;
    fragmentState.entryPoint = "fs_main";
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTargetState;

    wgpu::DepthStencilState depthStencilState{};
    depthStencilState.format = wgpu::TextureFormat::Depth24PlusStencil8;
    depthStencilState.depthWriteEnabled = true;
    depthStencilState.depthCompare = wgpu::CompareFunction::LessEqual;

    wgpu::BindGroupLayout bindGroupLayouts[] = {m_globalBindGroupLayout, m_modelBindGroupLayout};

    wgpu::PipelineLayoutDescriptor layoutDescriptor{};
    layoutDescriptor.bindGroupLayoutCount = 2;
    layoutDescriptor.bindGroupLayouts = bindGroupLayouts;

    wgpu::PipelineLayout pipelineLayout = m_device.CreatePipelineLayout(&layoutDescriptor);

    wgpu::RenderPipelineDescriptor descriptor{};
    descriptor.layout = pipelineLayout;
    descriptor.vertex.module = m_modelShaderModule;
    descriptor.vertex.entryPoint = "vs_main";
    descriptor.vertex.bufferCount = 1;
    descriptor.vertex.buffers = &vertexBufferLayout;
    descriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    descriptor.depthStencil = &depthStencilState;
    descriptor.fragment = &fragmentState;

    m_modelPipelineOpaque = m_device.CreateRenderPipeline(&descriptor);

    // Set up pipeline for transparent objects
    wgpu::BlendComponent blendComponent{};
    blendComponent.operation = wgpu::BlendOperation::Add;
    blendComponent.srcFactor = wgpu::BlendFactor::SrcAlpha;
    blendComponent.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;

    wgpu::BlendState blendState{};
    blendState.color = blendComponent;
    blendState.alpha = blendComponent;

    colorTargetState.blend = &blendState;
    depthStencilState.depthWriteEnabled = false; // Disable depth writes for transparent objects

    m_modelPipelineTransparent = m_device.CreateRenderPipeline(&descriptor);
}

void Renderer::CreateEnvironmentRenderPipeline()
{
    wgpu::ColorTargetState colorTargetState{};
    colorTargetState.format = m_surfaceFormat;

    wgpu::FragmentState fragmentState{};
    fragmentState.module = m_modelShaderModule;
    fragmentState.entryPoint = "fs_main";
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTargetState;

    wgpu::DepthStencilState depthStencilState{};
    depthStencilState.format = wgpu::TextureFormat::Depth24PlusStencil8;
    depthStencilState.depthWriteEnabled = true;
    depthStencilState.depthCompare = wgpu::CompareFunction::Less;

    // Create an environment pipeline
    wgpu::ShaderModuleWGSLDescriptor environmentWgslDesc{};
    const std::string environmentShader = LoadShaderFile("./assets/shaders/environment.wgsl");
    environmentWgslDesc.code = environmentShader.c_str();

    wgpu::ShaderModuleDescriptor environmentShaderModuleDescriptor{};
    environmentShaderModuleDescriptor.nextInChain = &environmentWgslDesc;
    m_environmentShaderModule = m_device.CreateShaderModule(&environmentShaderModuleDescriptor);

    wgpu::FragmentState environmentFragmentState{};
    environmentFragmentState.module = m_environmentShaderModule;
    environmentFragmentState.entryPoint = "fs_main";
    environmentFragmentState.targetCount = 1;
    environmentFragmentState.targets = &colorTargetState;

    wgpu::BindGroupLayout environmentBindGroupLayouts[] = {m_globalBindGroupLayout};
    wgpu::PipelineLayoutDescriptor environmentLayoutDescriptor{};
    environmentLayoutDescriptor.bindGroupLayoutCount = 1;
    environmentLayoutDescriptor.bindGroupLayouts = environmentBindGroupLayouts;
    wgpu::PipelineLayout environmentPipelineLayout = m_device.CreatePipelineLayout(&environmentLayoutDescriptor);

    depthStencilState.depthWriteEnabled = false; // Disable depth writes for the environment
    wgpu::RenderPipelineDescriptor environmentDescriptor{};
    environmentDescriptor.layout = environmentPipelineLayout;
    environmentDescriptor.vertex.module = m_environmentShaderModule;
    environmentDescriptor.vertex.entryPoint = "vs_main";
    environmentDescriptor.vertex.bufferCount = 0;
    environmentDescriptor.vertex.buffers = nullptr; // Vertices encoded in shader
    environmentDescriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    environmentDescriptor.depthStencil = &depthStencilState;
    environmentDescriptor.fragment = &environmentFragmentState;

    m_environmentPipeline = m_device.CreateRenderPipeline(&environmentDescriptor);
}

void Renderer::UpdateUniforms(const glm::mat4 &modelMatrix, const CameraUniformsInput &camera) const
{
    // Update the global uniforms
    GlobalUniforms globalUniforms;
    globalUniforms.viewMatrix = camera.viewMatrix;
    globalUniforms.projectionMatrix = camera.projectionMatrix;
    globalUniforms.inverseViewMatrix = glm::inverse(globalUniforms.viewMatrix);
    globalUniforms.inverseProjectionMatrix = glm::inverse(globalUniforms.projectionMatrix);
    globalUniforms.cameraPosition = camera.cameraPosition;

    // Upload the uniforms to the GPU
    m_device.GetQueue().WriteBuffer(m_globalUniformBuffer, 0, &globalUniforms, sizeof(GlobalUniforms));

    // Update the model uniforms
    ModelUniforms modelUniforms;
    modelUniforms.modelMatrix = modelMatrix;

    // Compute the normal matrix as a 3x3 matrix (inverse transpose of the model matrix)
    glm::mat3 normalMatrix3x3 = glm::transpose(glm::inverse(glm::mat3(modelUniforms.modelMatrix)));

    // Convert the normal matrix to a 4x4 matrix (upper-left 3x3 filled, rest is identity)
    modelUniforms.normalMatrix = glm::mat4(1.0f);                        // Initialize as identity
    modelUniforms.normalMatrix[0] = glm::vec4(normalMatrix3x3[0], 0.0f); // First row
    modelUniforms.normalMatrix[1] = glm::vec4(normalMatrix3x3[1], 0.0f); // Second row
    modelUniforms.normalMatrix[2] = glm::vec4(normalMatrix3x3[2], 0.0f); // Third row

    // Upload the uniforms to the GPU
    m_device.GetQueue().WriteBuffer(m_modelUniformBuffer, 0, &modelUniforms, sizeof(ModelUniforms));
}

void Renderer::SortTransparentMeshes(const glm::mat4 &modelMatrix, const glm::mat4 &viewMatrix)
{
    glm::mat4 modelView = viewMatrix * modelMatrix;

    m_transparentMeshesDepthSorted.clear();
    m_transparentMeshesDepthSorted.reserve(m_transparentMeshes.size());

    for (uint32_t i = 0; i < m_transparentMeshes.size(); ++i)
    {
        SubMesh &subMesh = m_transparentMeshes[i];

        glm::vec4 centroid = modelView * glm::vec4(subMesh.m_centroid, 1.0f);
        float depth = centroid.z;

        // Only add meshes in front of the camera
        if (depth < 0.0f)
        {
            SubMeshDepthInfo subMeshDepthInfo = {.m_depth = depth, .m_meshIndex = i};
            m_transparentMeshesDepthSorted.push_back(subMeshDepthInfo);
        }
    }

    // Sort the transparent meshes based on depth (back to front; highest negative Z being furthest away)
    std::sort(m_transparentMeshesDepthSorted.begin(), m_transparentMeshesDepthSorted.end(),
              [](const SubMeshDepthInfo &a, const SubMeshDepthInfo &b) { return a.m_depth < b.m_depth; });
}

void Renderer::GetAdapter(const std::function<void(wgpu::Adapter)> &callback)
{
    wgpu::RequestAdapterOptions options{};
    options.compatibleSurface = m_surface;
    options.powerPreference = wgpu::PowerPreference::HighPerformance;

    m_instance.RequestAdapter(
        &options,
        [](WGPURequestAdapterStatus status, WGPUAdapter cAdapter, const char *message, void *userdata) {
            if (message)
            {
                std::cerr << "RequestAdapter: " << message << std::endl;
            }
            if (status != WGPURequestAdapterStatus_Success)
            {
                std::cerr << "Failed to request adapter." << std::endl;
                return;
            }
            wgpu::Adapter adapter = wgpu::Adapter::Acquire(cAdapter);
            auto cb = *static_cast<std::function<void(wgpu::Adapter)> *>(userdata);
            cb(adapter);                                                        // Execute the callback
            delete static_cast<std::function<void(wgpu::Adapter)> *>(userdata); // Clean up
        },
        new std::function<void(wgpu::Adapter)>(callback));
}

void Renderer::GetDevice(const std::function<void(wgpu::Device)> &callback)
{
    wgpu::DeviceDescriptor deviceDesc;

    // Helper function to log device lost reasons
    auto logDeviceLostReason = [](auto reason, const char *message) {
        std::cerr << "Device lost: ";
        switch (reason)
        {
#if defined(__EMSCRIPTEN__)
        case WGPUDeviceLostReason_Unknown:
            std::cerr << "[Reason: Unknown]";
            break;
        case WGPUDeviceLostReason_Destroyed:
            std::cerr << "[Reason: Destroyed]";
            break;
#else
        // Dawn/native device lost reasons
        case wgpu::DeviceLostReason::Unknown:
            std::cerr << "[Reason: Unknown]";
            break;
        case wgpu::DeviceLostReason::Destroyed:
            std::cerr << "[Reason: Destroyed]";
            break;
        case wgpu::DeviceLostReason::InstanceDropped:
            std::cerr << "[Reason: Instance Dropped]";
            break;
        case wgpu::DeviceLostReason::FailedCreation:
            std::cerr << "[Reason: Failed Creation]";
            break;
#endif
        default:
            std::cerr << "[Reason: Unrecognized]";
            break;
        }
        if (message)
        {
            std::cerr << " - " << message << std::endl;
        }
        else
        {
            std::cerr << " - No message provided." << std::endl;
        }
    };

#if defined(__EMSCRIPTEN__)
    // Set device lost callback for Emscripten
    deviceDesc.deviceLostCallback = [](WGPUDeviceLostReason reason, const char *message, void *userdata) {
        auto logFn = static_cast<decltype(logDeviceLostReason) *>(userdata);
        (*logFn)(reason, message); // Call the logging function
    };
    deviceDesc.deviceLostUserdata = &logDeviceLostReason; // Pass logDeviceLostReason as userdata
#else
    // Set device lost callback for Dawn/native
    deviceDesc.SetDeviceLostCallback(
        wgpu::CallbackMode::AllowSpontaneous,
        [logDeviceLostReason]([[maybe_unused]] const wgpu::Device &device, wgpu::DeviceLostReason reason,
                              const char *message) { logDeviceLostReason(reason, message); });

    // Set uncaptured error callback for Dawn/native
    deviceDesc.SetUncapturedErrorCallback(
        []([[maybe_unused]] const wgpu::Device &device, [[maybe_unused]] wgpu::ErrorType type, const char *message) {
            std::cerr << "Uncaptured error: ";
            if (message)
            {
                std::cerr << message << std::endl;
            }
            else
            {
                std::cerr << "No message provided." << std::endl;
            }
            std::exit(EXIT_FAILURE); // Exit the application
        });
#endif

    m_adapter.RequestDevice(
        &deviceDesc,
        [](WGPURequestDeviceStatus status, WGPUDevice cDevice, const char *message, void *userdata) {
            if (message)
            {
                std::cerr << "RequestDevice: " << message << std::endl;
            }
            if (status != WGPURequestDeviceStatus_Success)
            {
                std::cerr << "Failed to request device." << std::endl;
                return;
            }
            wgpu::Device device = wgpu::Device::Acquire(cDevice);
            auto cb = *static_cast<std::function<void(wgpu::Device)> *>(userdata);
            cb(device);                                                        // Execute the callback
            delete static_cast<std::function<void(wgpu::Device)> *>(userdata); // Clean up
        },
        new std::function<void(wgpu::Device)>(callback));
}

std::string Renderer::LoadShaderFile(const std::string &filepath) const
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