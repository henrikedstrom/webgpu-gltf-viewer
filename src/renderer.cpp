// Standard Library Headers
#include <algorithm>
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
#include <glm/gtx/polar_coordinates.hpp>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#else
#include <webgpu/webgpu_glfw.h>
#endif

// Project Headers
#include "application.h"
#include "camera.h"
#include "environment.h"
#include "environment_preprocessor.h"
#include "mipmap_generator.h"
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
void CreateTexture(const TextureInfo *textureInfo, wgpu::TextureFormat format, glm::vec4 defaultValue, wgpu::Device device,
                   MipmapGenerator &mipmapGenerator, wgpu::Texture &texture, wgpu::TextureView &textureView)
{
    // Set default pixel value
    const uint8_t defaultPixel[4] = {static_cast<uint8_t>(defaultValue.r * 255.0f),
                                     static_cast<uint8_t>(defaultValue.g * 255.0f),
                                     static_cast<uint8_t>(defaultValue.b * 255.0f),
                                     static_cast<uint8_t>(defaultValue.a * 255.0f)};
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

    // Create a WebGPU texture descriptor with mipmapping enabled
    wgpu::TextureDescriptor textureDescriptor{};
    textureDescriptor.size = {width, height, 1};
    textureDescriptor.format = wgpu::TextureFormat::RGBA8Unorm;
    textureDescriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::StorageBinding |
                              wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::CopySrc;
    textureDescriptor.mipLevelCount = mipLevelCount;

    // Create an intermediate texture for generating mipmaps
    wgpu::Texture intermediateTexture = device.CreateTexture(&textureDescriptor);

    // Upload the texture data
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

    // Generate mipmaps
    mipmapGenerator.GenerateMipmaps(intermediateTexture, textureDescriptor.size, false);

    // Set TextureDescriptor properties for the final texture
    textureDescriptor.format = format;
    textureDescriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;

    // Create the final texture
    texture = device.CreateTexture(&textureDescriptor);

    // Copy the intermediate texture to the final texture
    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();

    for (uint32_t level = 0; level < mipLevelCount; ++level)
    {
        uint32_t mipWidth = std::max(width >> level, 1u);
        uint32_t mipHeight = std::max(height >> level, 1u);
        wgpu::ImageCopyTexture src = {
            .texture = intermediateTexture, .mipLevel = level, .origin = {0, 0, 0}, .aspect = wgpu::TextureAspect::All};
        wgpu::ImageCopyTexture dst = {
            .texture = texture, .mipLevel = level, .origin = {0, 0, 0}, .aspect = wgpu::TextureAspect::All};
        wgpu::Extent3D extent = {mipWidth, mipHeight, 1};
        encoder.CopyTextureToTexture(&src, &dst, &extent);
    }

    wgpu::CommandBuffer commandBuffer = encoder.Finish();
    device.GetQueue().Submit(1, &commandBuffer);

    // Create a texture view covering all mip levels
    wgpu::TextureViewDescriptor viewDescriptor{};
    viewDescriptor.format = format;
    viewDescriptor.dimension = wgpu::TextureViewDimension::e2D;
    viewDescriptor.mipLevelCount = mipLevelCount;
    viewDescriptor.arrayLayerCount = 1;

    textureView = texture.CreateView(&viewDescriptor);
}

template <typename TextureInfo>
void CreateTextureCube(const TextureInfo *textureInfo, wgpu::Device device, MipmapGenerator &mipmapGenerator,
                       wgpu::Texture &texture, wgpu::TextureView &textureView)
{
    uint32_t width = textureInfo->m_width;
    uint32_t height = textureInfo->m_height;

    // Compute the number of mip levels
    uint32_t mipLevelCount = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;

    // Create a WebGPU texture descriptor with mipmapping enabled
    wgpu::TextureDescriptor textureDescriptor{};
    textureDescriptor.size = {width, height, 6};
    textureDescriptor.format = wgpu::TextureFormat::RGBA16Float;
    textureDescriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::StorageBinding |
                              wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::CopySrc;
    textureDescriptor.mipLevelCount = mipLevelCount;

    texture = device.CreateTexture(&textureDescriptor);

    // Upload the texture data
    for (uint32_t face = 0; face < 6; ++face)
    {
        const Float16 *data = textureInfo->m_data[face].data();
        wgpu::Extent3D textureSize = {width, height, 1};

        wgpu::ImageCopyTexture imageCopyTexture{};
        imageCopyTexture.texture = texture;
        imageCopyTexture.mipLevel = 0;
        imageCopyTexture.origin = {0, 0, face};
        imageCopyTexture.aspect = wgpu::TextureAspect::All;

        wgpu::TextureDataLayout source;
        source.offset = 0;
        source.bytesPerRow = 4 * width * sizeof(Float16);
        source.rowsPerImage = height;

        wgpu::Extent3D faceSize = {width, height, 1};
        device.GetQueue().WriteTexture(&imageCopyTexture, data, 4 * width * height * sizeof(Float16), &source,
                                       &faceSize);
    }

    // Generate mipmaps
    mipmapGenerator.GenerateMipmaps(texture, textureDescriptor.size, true);

    // Create a texture view covering all mip levels
    wgpu::TextureViewDescriptor viewDescriptor{};
    viewDescriptor.format = wgpu::TextureFormat::RGBA16Float;
    viewDescriptor.dimension = wgpu::TextureViewDimension::Cube;
    viewDescriptor.mipLevelCount = mipLevelCount;
    viewDescriptor.arrayLayerCount = 6;

    textureView = texture.CreateView(&viewDescriptor);
}

void CreateEnvironmentTexture(wgpu::Device device, wgpu::TextureViewDimension type, wgpu::Extent3D size,
                              bool mipmapping, wgpu::Texture &texture, wgpu::TextureView &textureView)
{
    // Compute the number of mip levels
    const uint32_t mipLevelCount = mipmapping ? static_cast<uint32_t>(std::log2(std::max(size.width, size.height))) + 1 : 1;

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

void Renderer::Initialize(GLFWwindow *window, Camera *camera, Environment *environment, const Model &model,
                          uint32_t width, uint32_t height, const std::function<void()> &callback)
{
    m_window = window;
    m_camera = camera;
    m_environment = environment;
    m_model = &model;
    m_width = width;
    m_height = height;

    m_instance = wgpu::CreateInstance();
    GetAdapter([this, callback, &model](wgpu::Adapter adapter) {
        m_adapter = adapter;
        GetDevice([this, callback, &model](wgpu::Device device) {
            m_device = device;

#if defined(__EMSCRIPTEN__)
            wgpu::SurfaceDescriptorFromCanvasHTMLSelector canvasDesc{};
            canvasDesc.selector = "#canvas";
            wgpu::SurfaceDescriptor surfaceDesc{.nextInChain = &canvasDesc};
            m_surface = m_instance.CreateSurface(&surfaceDesc);
#else
            m_surface = wgpu::glfw::CreateSurfaceForWindow(m_instance, m_window);
#endif

            InitGraphics(model);

            // Return control to the application
            callback();
        });
    });
}

void Renderer::Resize(uint32_t width, uint32_t height)
{
    m_width = width;
    m_height = height;

    // Recreate the depth texture
    CreateDepthTexture();

    // Update the surface configuration
    ConfigureSurface();
}

void Renderer::Render()
{
    const glm::mat4 modelMatrix = m_model->GetTransform();
    UpdateUniforms(modelMatrix);
    SortTransparentMeshes(modelMatrix);

    wgpu::SurfaceTexture surfaceTexture;
    m_surface.GetCurrentTexture(&surfaceTexture);
    if (!surfaceTexture.texture)
    {
        std::cerr << "Error: Failed to get current surface texture." << std::endl;
        return;
    }

    wgpu::RenderPassColorAttachment attachment{.view = surfaceTexture.texture.CreateView(),
                                               .loadOp = wgpu::LoadOp::Clear,
                                               .storeOp = wgpu::StoreOp::Store,
                                               .clearValue = {.r = 0.0f, .g = 0.2f, .b = 0.4f, .a = 1.0f}};

    wgpu::RenderPassDepthStencilAttachment depthAttachment{
        .view = m_depthTextureView,
        .depthLoadOp = wgpu::LoadOp::Clear,
        .depthStoreOp = wgpu::StoreOp::Store,
        .depthClearValue = 1.0f, // Clear to the farthest value
        .stencilLoadOp = wgpu::LoadOp::Clear,
        .stencilStoreOp = wgpu::StoreOp::Store,
    };

    wgpu::RenderPassDescriptor renderpass{
        .colorAttachmentCount = 1, .colorAttachments = &attachment, .depthStencilAttachment = &depthAttachment};

    wgpu::CommandEncoder encoder = m_device.CreateCommandEncoder();
    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&renderpass);

    // Set global bind group
    pass.SetBindGroup(0, m_globalBindGroup);

    // Render the environment
    pass.SetPipeline(m_environmentPipeline);
    pass.Draw(6, 1, 0, 0);

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

    wgpu::CommandBuffer commands = encoder.Finish();
    m_device.GetQueue().Submit(1, &commands);

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
    // Store the new model reference
    m_model = &model;
    
    // Destroy the existing model resources
    m_vertexBuffer = nullptr;
    m_indexBuffer = nullptr;

    m_sampler = nullptr;
    m_modelShaderModule = nullptr;
    m_modelPipelineOpaque = nullptr;
    m_modelPipelineTransparent = nullptr;

    // Create new model resources
    CreateVertexBuffer(model);
    CreateIndexBuffer(model);
    CreateSubMeshes(model);
    CreateMaterials(model);
    CreateModelRenderPipelines();
}

void Renderer::UpdateEnvironment(const Environment &environment)
{
    // Destroy the existing environment resources
    m_environmentTexture = nullptr;
    m_environmentTextureView = nullptr;
    m_iblIrradianceTexture = nullptr;
    m_iblIrradianceTextureView = nullptr;
    m_iblSpecularTexture = nullptr;
    m_iblSpecularTextureView = nullptr;
    m_iblBrdfIntegrationLUT = nullptr;
    m_iblBrdfIntegrationLUTView = nullptr;
    m_environmentCubeSampler = nullptr;
    m_iblBrdfIntegrationLUTSampler = nullptr;
    m_environmentShaderModule = nullptr;
    m_environmentPipeline = nullptr;

    // Create new environment resources
    CreateEnvironmentTexturesAndSamplers();
    CreateGlobalBindGroup();
    CreateEnvironmentRenderPipeline();
}

void Renderer::InitGraphics(const Model &model)
{
    ConfigureSurface();
    CreateDepthTexture();

    CreateBindGroupLayouts();

    CreateUniformBuffers();

    UpdateEnvironment(*m_environment);

    UpdateModel(model);
}

void Renderer::ConfigureSurface()
{
    wgpu::SurfaceCapabilities capabilities;
    m_surface.GetCapabilities(m_adapter, &capabilities);
    m_surfaceFormat = capabilities.formats[0];
    wgpu::SurfaceConfiguration config{
        .device = m_device, .format = m_surfaceFormat, .width = m_width, .height = m_height};
    m_surface.Configure(&config);
}

void Renderer::CreateDepthTexture()
{
    wgpu::TextureDescriptor depthTextureDescriptor{};
    depthTextureDescriptor.size = {m_width, m_height, 1};
    depthTextureDescriptor.format = wgpu::TextureFormat::Depth24PlusStencil8;
    depthTextureDescriptor.usage = wgpu::TextureUsage::RenderAttachment;

    m_depthTexture = m_device.CreateTexture(&depthTextureDescriptor);
    m_depthTextureView = m_depthTexture.CreateView();
}

void Renderer::CreateBindGroupLayouts()
{
    wgpu::BindGroupLayoutEntry globalLayoutEntries[] = {
        {
            .binding = 0,
            .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
            .buffer = {.type = wgpu::BufferBindingType::Uniform,
                       .hasDynamicOffset = false,
                       .minBindingSize = sizeof(GlobalUniforms)},
        },
        {
            // Sampler binding
            .binding = 1,
            .visibility = wgpu::ShaderStage::Fragment,
            .sampler = {.type = wgpu::SamplerBindingType::Filtering},
        },
        {
            // Environment texture binding
            .binding = 2,
            .visibility = wgpu::ShaderStage::Fragment,
            .texture = {.sampleType = wgpu::TextureSampleType::Float,
                        .viewDimension = wgpu::TextureViewDimension::Cube,
                        .multisampled = false},
        },
        {
            // IBL irradiance texture binding
            .binding = 3,
            .visibility = wgpu::ShaderStage::Fragment,
            .texture = {.sampleType = wgpu::TextureSampleType::Float,
                        .viewDimension = wgpu::TextureViewDimension::Cube,
                        .multisampled = false},
        },
        {
            // IBL specular texture binding
            .binding = 4,
            .visibility = wgpu::ShaderStage::Fragment,
            .texture = {.sampleType = wgpu::TextureSampleType::Float,
                        .viewDimension = wgpu::TextureViewDimension::Cube,
                        .multisampled = false},
        },
        {
            // IBL LUT texture binding
            .binding = 5,
            .visibility = wgpu::ShaderStage::Fragment,
            .texture = {.sampleType = wgpu::TextureSampleType::Float,
                        .viewDimension = wgpu::TextureViewDimension::e2D,
                        .multisampled = false},
        },
        {
            // IBL LUT sampler binding
            .binding = 6,
            .visibility = wgpu::ShaderStage::Fragment,
            .sampler = {.type = wgpu::SamplerBindingType::Filtering},
        },
    };

    wgpu::BindGroupLayoutDescriptor globalBindGroupLayoutDescriptor{
        .entryCount = 7,
        .entries = globalLayoutEntries,
    };

    m_globalBindGroupLayout = m_device.CreateBindGroupLayout(&globalBindGroupLayoutDescriptor);

    wgpu::BindGroupLayoutEntry modelLayoutEntries[8] = {
        {
            .binding = 0,
            .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
            .buffer = {.type = wgpu::BufferBindingType::Uniform,
                       .hasDynamicOffset = false,
                       .minBindingSize = sizeof(ModelUniforms)},
        },
        {
            .binding = 1,
            .visibility = wgpu::ShaderStage::Fragment,
            .buffer = {.type = wgpu::BufferBindingType::Uniform,
                       .hasDynamicOffset = false,
                       .minBindingSize = sizeof(MaterialUniforms)},
        },
        {
            // Sampler binding
            .binding = 2,
            .visibility = wgpu::ShaderStage::Fragment,
            .sampler = {.type = wgpu::SamplerBindingType::Filtering},
        },
        {
            // Base color texture binding
            .binding = 3,
            .visibility = wgpu::ShaderStage::Fragment,
            .texture = {.sampleType = wgpu::TextureSampleType::Float,
                        .viewDimension = wgpu::TextureViewDimension::e2D,
                        .multisampled = false},
        },
        {
            // Metallic-Roughness texture binding
            .binding = 4,
            .visibility = wgpu::ShaderStage::Fragment,
            .texture = {.sampleType = wgpu::TextureSampleType::Float,
                        .viewDimension = wgpu::TextureViewDimension::e2D,
                        .multisampled = false},
        },
        {
            // Normal texture binding
            .binding = 5,
            .visibility = wgpu::ShaderStage::Fragment,
            .texture = {.sampleType = wgpu::TextureSampleType::Float,
                        .viewDimension = wgpu::TextureViewDimension::e2D,
                        .multisampled = false},
        },
        {
            // Occlusion texture binding
            .binding = 6,
            .visibility = wgpu::ShaderStage::Fragment,
            .texture = {.sampleType = wgpu::TextureSampleType::Float,
                        .viewDimension = wgpu::TextureViewDimension::e2D,
                        .multisampled = false},
        },
        {
            // Emissive texture binding
            .binding = 7,
            .visibility = wgpu::ShaderStage::Fragment,
            .texture = {.sampleType = wgpu::TextureSampleType::Float,
                        .viewDimension = wgpu::TextureViewDimension::e2D,
                        .multisampled = false},
        },

    };

    wgpu::BindGroupLayoutDescriptor modelBindGroupLayoutDescriptor{
        .entryCount = 8,
        .entries = modelLayoutEntries,
    };

    m_modelBindGroupLayout = m_device.CreateBindGroupLayout(&modelBindGroupLayoutDescriptor);
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

void Renderer::CreateEnvironmentTexturesAndSamplers()
{
    MipmapGenerator mipmapGenerator(m_device);

    const Environment::Texture &panoramaTexture = m_environment->GetTexture();
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
    mipmapGenerator.GenerateMipmaps(m_environmentTexture, {environmentCubeSize, environmentCubeSize, 6}, true);

    // Precompute IBL maps
    EnvironmentPreprocessor environmentPreprocessor(m_device);
    environmentPreprocessor.GenerateMaps(m_environmentTexture, m_iblIrradianceTexture, m_iblSpecularTexture,
                                         m_iblBrdfIntegrationLUT);
    mipmapGenerator.GenerateMipmaps(m_iblIrradianceTexture, {kIrradianceMapSize, kIrradianceMapSize, 6}, true);

    // Create a sampler for the environment texture
    wgpu::SamplerDescriptor samplerDescriptor{};
    samplerDescriptor.addressModeU = wgpu::AddressMode::Repeat;
    samplerDescriptor.addressModeV = wgpu::AddressMode::Repeat;
    samplerDescriptor.addressModeW = wgpu::AddressMode::Repeat;
    samplerDescriptor.minFilter = wgpu::FilterMode::Linear;
    samplerDescriptor.magFilter = wgpu::FilterMode::Linear;
    samplerDescriptor.mipmapFilter = wgpu::MipmapFilterMode::Linear;
    m_environmentCubeSampler = m_device.CreateSampler(&samplerDescriptor);

    // Create a sampler for the IBL BRDF LUT texture
    samplerDescriptor.addressModeU = wgpu::AddressMode::ClampToEdge;
    samplerDescriptor.addressModeV = wgpu::AddressMode::ClampToEdge;
    samplerDescriptor.addressModeW = wgpu::AddressMode::ClampToEdge;
    samplerDescriptor.minFilter = wgpu::FilterMode::Linear;
    samplerDescriptor.magFilter = wgpu::FilterMode::Linear;
    samplerDescriptor.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
    m_iblBrdfIntegrationLUTSampler = m_device.CreateSampler(&samplerDescriptor);
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
        // Create a sampler for model textures
        wgpu::SamplerDescriptor samplerDescriptor{};
        samplerDescriptor.addressModeU = wgpu::AddressMode::Repeat;
        samplerDescriptor.addressModeV = wgpu::AddressMode::Repeat;
        samplerDescriptor.addressModeW = wgpu::AddressMode::Repeat;
        samplerDescriptor.minFilter = wgpu::FilterMode::Linear;
        samplerDescriptor.magFilter = wgpu::FilterMode::Linear;
        samplerDescriptor.mipmapFilter = wgpu::MipmapFilterMode::Linear;
        m_sampler = m_device.CreateSampler(&samplerDescriptor);

        MipmapGenerator mipmapGenerator(m_device);

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
            
            glm::vec4 defaultBaseColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            glm::vec4 defaultMetallicRoughness = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            glm::vec4 defaultNormal = glm::vec4(0.5f, 0.5f, 1.0f, 1.0f);
            glm::vec4 defaultOcculsion = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
            glm::vec4 defaultEmissive = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

            // Base Color Texture
            CreateTexture(model.GetTexture(srcMat.m_baseColorTexture), wgpu::TextureFormat::RGBA8UnormSrgb,
                          defaultBaseColor, m_device, mipmapGenerator, dstMat.m_baseColorTexture,
                          dstMat.m_baseColorTextureView);

            // Metallic-Roughness
            CreateTexture(model.GetTexture(srcMat.m_metallicRoughnessTexture), wgpu::TextureFormat::RGBA8Unorm,
                          defaultMetallicRoughness, m_device, mipmapGenerator, dstMat.m_metallicRoughnessTexture,
                          dstMat.m_metallicRoughnessTextureView);

            // Normal Texture
            CreateTexture(model.GetTexture(srcMat.m_normalTexture), wgpu::TextureFormat::RGBA8Unorm, defaultNormal,
                          m_device, mipmapGenerator, dstMat.m_normalTexture, dstMat.m_normalTextureView);

            // Occlusion Texture
            CreateTexture(model.GetTexture(srcMat.m_occlusionTexture), wgpu::TextureFormat::RGBA8Unorm,
                          defaultOcculsion, m_device, mipmapGenerator, dstMat.m_occlusionTexture,
                          dstMat.m_occlusionTextureView);

            // Emissive Texture
            CreateTexture(model.GetTexture(srcMat.m_emissiveTexture), wgpu::TextureFormat::RGBA8UnormSrgb,
                          defaultEmissive, m_device, mipmapGenerator, dstMat.m_emissiveTexture,
                          dstMat.m_emissiveTextureView);

            // Create bind group
            wgpu::BindGroupEntry bindGroupEntries[8] = {{
                                                            .binding = 0,
                                                            .buffer = m_modelUniformBuffer,
                                                            .offset = 0,
                                                            .size = sizeof(ModelUniforms),
                                                        },
                                                        {
                                                            .binding = 1,
                                                            .buffer = dstMat.m_uniformBuffer,
                                                            .offset = 0,
                                                            .size = sizeof(MaterialUniforms),
                                                        },
                                                        {
                                                            .binding = 2,
                                                            .sampler = m_sampler,
                                                        },
                                                        {
                                                            .binding = 3,
                                                            .textureView = dstMat.m_baseColorTextureView,
                                                        },
                                                        {
                                                            .binding = 4,
                                                            .textureView = dstMat.m_metallicRoughnessTextureView,
                                                        },
                                                        {
                                                            .binding = 5,
                                                            .textureView = dstMat.m_normalTextureView,
                                                        },
                                                        {
                                                            .binding = 6,
                                                            .textureView = dstMat.m_occlusionTextureView,
                                                        },
                                                        {
                                                            .binding = 7,
                                                            .textureView = dstMat.m_emissiveTextureView,
                                                        }};

            wgpu::BindGroupDescriptor bindGroupDescriptor{
                .layout = m_modelBindGroupLayout,
                .entryCount = 8,
                .entries = bindGroupEntries,
            };

            dstMat.m_bindGroup = m_device.CreateBindGroup(&bindGroupDescriptor);
        }
    }
}

void Renderer::CreateGlobalBindGroup()
{
    wgpu::BindGroupEntry bindGroupEntries[] = {
        {
            .binding = 0,
            .buffer = m_globalUniformBuffer,
            .offset = 0,
            .size = sizeof(GlobalUniforms),
        },
        {
            .binding = 1,
            .sampler = m_environmentCubeSampler,
        },
        {
            .binding = 2,
            .textureView = m_environmentTextureView,
        },
        {
            .binding = 3,
            .textureView = m_iblIrradianceTextureView,
        },
        {
            .binding = 4,
            .textureView = m_iblSpecularTextureView,
        },
        {
            .binding = 5,
            .textureView = m_iblBrdfIntegrationLUTView,
        },
        {
            .binding = 6,
            .sampler = m_iblBrdfIntegrationLUTSampler,
        },
    };

    wgpu::BindGroupDescriptor bindGroupDescriptor{
        .layout = m_globalBindGroupLayout,
        .entryCount = 7,
        .entries = bindGroupEntries,
    };

    m_globalBindGroup = m_device.CreateBindGroup(&bindGroupDescriptor);
}

void Renderer::CreateModelRenderPipelines()
{
    wgpu::ShaderModuleWGSLDescriptor wgslDesc{};
    const std::string shader = LoadShaderFile("./assets/shaders/gltf_pbr.wgsl");
    wgslDesc.code = shader.c_str();

    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{.nextInChain = &wgslDesc};
    m_modelShaderModule = m_device.CreateShaderModule(&shaderModuleDescriptor);

    wgpu::VertexAttribute vertexAttributes[] = {
        {.format = wgpu::VertexFormat::Float32x3, .offset = offsetof(Model::Vertex, m_position), .shaderLocation = 0},
        {.format = wgpu::VertexFormat::Float32x3, .offset = offsetof(Model::Vertex, m_normal), .shaderLocation = 1},
        {.format = wgpu::VertexFormat::Float32x4, .offset = offsetof(Model::Vertex, m_tangent), .shaderLocation = 2},
        {.format = wgpu::VertexFormat::Float32x2, .offset = offsetof(Model::Vertex, m_texCoord0), .shaderLocation = 3},
        {.format = wgpu::VertexFormat::Float32x2, .offset = offsetof(Model::Vertex, m_texCoord1), .shaderLocation = 4},
        {.format = wgpu::VertexFormat::Float32x4, .offset = offsetof(Model::Vertex, m_color), .shaderLocation = 5},
    };

    wgpu::VertexBufferLayout vertexBufferLayout{
        .arrayStride = sizeof(Model::Vertex),
        .stepMode = wgpu::VertexStepMode::Vertex,
        .attributeCount = 6,
        .attributes = vertexAttributes,
    };

    wgpu::ColorTargetState colorTargetState{.format = m_surfaceFormat};

    wgpu::FragmentState fragmentState{
        .module = m_modelShaderModule, .entryPoint = "fragmentMain", .targetCount = 1, .targets = &colorTargetState};

    wgpu::DepthStencilState depthStencilState{
        .format = wgpu::TextureFormat::Depth24PlusStencil8,
        .depthWriteEnabled = true,
        .depthCompare = wgpu::CompareFunction::Less,
    };

    wgpu::BindGroupLayout bindGroupLayouts[] = {m_globalBindGroupLayout, m_modelBindGroupLayout};

    wgpu::PipelineLayoutDescriptor layoutDescriptor{
        .bindGroupLayoutCount = 2,
        .bindGroupLayouts = bindGroupLayouts,
    };

    wgpu::PipelineLayout pipelineLayout = m_device.CreatePipelineLayout(&layoutDescriptor);

    wgpu::RenderPipelineDescriptor descriptor{.layout = pipelineLayout,
                                              .vertex =
                                                  {
                                                      .module = m_modelShaderModule,
                                                      .entryPoint = "vertexMain",
                                                      .bufferCount = 1,
                                                      .buffers = &vertexBufferLayout,
                                                  },
                                              .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
                                              .depthStencil = &depthStencilState,
                                              .fragment = &fragmentState};

    m_modelPipelineOpaque = m_device.CreateRenderPipeline(&descriptor);

    // Set up pipeline for transparent objects
    wgpu::BlendComponent blendComponent = {
        .operation = wgpu::BlendOperation::Add,
        .srcFactor = wgpu::BlendFactor::SrcAlpha,
        .dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha,
    };

    wgpu::BlendState blendState = {
        .color = blendComponent,
        .alpha = blendComponent,
    };

    colorTargetState.blend = &blendState;
    depthStencilState.depthWriteEnabled = false; // Disable depth writes for transparent objects

    m_modelPipelineTransparent = m_device.CreateRenderPipeline(&descriptor);
}

void Renderer::CreateEnvironmentRenderPipeline()
{
    wgpu::ColorTargetState colorTargetState{.format = m_surfaceFormat};

    wgpu::FragmentState fragmentState{
        .module = m_modelShaderModule, .entryPoint = "fragmentMain", .targetCount = 1, .targets = &colorTargetState};

    wgpu::DepthStencilState depthStencilState{
        .format = wgpu::TextureFormat::Depth24PlusStencil8,
        .depthWriteEnabled = true,
        .depthCompare = wgpu::CompareFunction::Less,
    };

    // Create an environment pipeline
    wgpu::ShaderModuleWGSLDescriptor environmentWgslDesc{};
    const std::string environmentShader = LoadShaderFile("./assets/shaders/environment.wgsl");
    environmentWgslDesc.code = environmentShader.c_str();

    wgpu::ShaderModuleDescriptor environmentShaderModuleDescriptor{.nextInChain = &environmentWgslDesc};
    m_environmentShaderModule = m_device.CreateShaderModule(&environmentShaderModuleDescriptor);

    wgpu::FragmentState environmentFragmentState{.module = m_environmentShaderModule,
                                                 .entryPoint = "fragmentMain",
                                                 .targetCount = 1,
                                                 .targets = &colorTargetState};

    wgpu::BindGroupLayout environmentBindGroupLayouts[] = {m_globalBindGroupLayout};
    wgpu::PipelineLayoutDescriptor environmentLayoutDescriptor{
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = environmentBindGroupLayouts,
    };
    wgpu::PipelineLayout environmentPipelineLayout = m_device.CreatePipelineLayout(&environmentLayoutDescriptor);

    depthStencilState.depthWriteEnabled = false; // Disable depth writes for the environment
    wgpu::RenderPipelineDescriptor environmentDescriptor{
        .layout = environmentPipelineLayout,
        .vertex =
            {
                .module = m_environmentShaderModule,
                .entryPoint = "vertexMain",
                .bufferCount = 0,
                .buffers = nullptr, // Vertices encoded in shader
            },
        .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
        .depthStencil = &depthStencilState,
        .fragment = &environmentFragmentState};

    m_environmentPipeline = m_device.CreateRenderPipeline(&environmentDescriptor);
}

void Renderer::UpdateUniforms(const glm::mat4 &modelMatrix) const
{
    // Update the global uniforms
    GlobalUniforms globalUniforms;
    globalUniforms.viewMatrix = m_camera->GetViewMatrix();
    globalUniforms.projectionMatrix = m_camera->GetProjectionMatrix();
    globalUniforms.inverseViewMatrix = glm::inverse(globalUniforms.viewMatrix);
    globalUniforms.inverseProjectionMatrix = glm::inverse(globalUniforms.projectionMatrix);
    globalUniforms.cameraPosition = m_camera->GetWorldPosition();

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

void Renderer::SortTransparentMeshes(const glm::mat4 &modelMatrix)
{
    glm::mat4 modelView = m_camera->GetViewMatrix() * modelMatrix;

    m_transparentMeshesDepthSorted.clear();
    m_transparentMeshesDepthSorted.reserve(m_transparentMeshes.size());

    for (uint32_t i = 0; i < m_transparentMeshes.size(); ++i)
    {
        SubMesh &subMesh = m_transparentMeshes[i];

        glm::vec4 centroid = modelView * glm::vec4(subMesh.m_centroid, 1.0f);
        float depth = centroid.z;

        // Only add meshes in front of the camera
        if (depth < 0.0f) {
            SubMeshDepthInfo subMeshDepthInfo = {
                .m_depth = depth,
                .m_meshIndex = i
            };
            m_transparentMeshesDepthSorted.push_back(subMeshDepthInfo);
        }
    }

    // Sort the transparent meshes based on depth (back to front; highest negative Z being furthest away)
    std::sort(m_transparentMeshesDepthSorted.begin(), m_transparentMeshesDepthSorted.end(),
              [](const SubMeshDepthInfo &a, const SubMeshDepthInfo &b) {
                  return a.m_depth < b.m_depth;
              });
}

void Renderer::GetAdapter(const std::function<void(wgpu::Adapter)> &callback)
{
    wgpu::RequestAdapterOptions options{.compatibleSurface = m_surface,
                                        .powerPreference = wgpu::PowerPreference::HighPerformance};

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
        [logDeviceLostReason](const wgpu::Device &device, wgpu::DeviceLostReason reason, const char *message) {
            logDeviceLostReason(reason, message);
        });

    // Set uncaptured error callback for Dawn/native
    deviceDesc.SetUncapturedErrorCallback([](const wgpu::Device &device, wgpu::ErrorType type, const char *message) {
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