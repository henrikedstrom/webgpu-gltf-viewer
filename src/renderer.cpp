// Standard Library Headers
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
#include "model.h"
#include "orbit_controls.h"
#include "renderer.h"

//----------------------------------------------------------------------
// Internal Utility Functions

namespace
{
void CreateTexture(const Model::Texture *textureInfo, wgpu::Device device, wgpu::Texture &texture,
                   wgpu::TextureView &textureView)
{
    // Default to 1x1 black texture (in case texture is missing)
    const uint8_t blackPixel[] = {0, 0, 0, 255};
    uint32_t width = 1;
    uint32_t height = 1;
    const uint8_t *data = const_cast<uint8_t *>(blackPixel);

    if (textureInfo)
    {
        width = textureInfo->m_width;
        height = textureInfo->m_height;
        data = textureInfo->m_data.data();
    }

    // Create a WebGPU texture descriptor
    wgpu::TextureDescriptor textureDescriptor{};
    textureDescriptor.size = {width, height, 1};
    textureDescriptor.format = wgpu::TextureFormat::RGBA8Unorm;
    textureDescriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;

    texture = device.CreateTexture(&textureDescriptor);

    // Write the image data to the texture
    wgpu::ImageCopyTexture imageCopyTexture{};
    imageCopyTexture.texture = texture;
    imageCopyTexture.mipLevel = 0;

    wgpu::Extent3D extent{width, height, 1};

    wgpu::TextureDataLayout textureDataLayout{
        .offset = 0,
        .bytesPerRow = width * 4, // RGBA8
        .rowsPerImage = height,
    };

    device.GetQueue().WriteTexture(&imageCopyTexture, data, width * 4 * height, &textureDataLayout, &extent);

    // Create a texture view
    wgpu::TextureViewDescriptor viewDescriptor{};
    viewDescriptor.format = wgpu::TextureFormat::RGBA8Unorm;
    viewDescriptor.dimension = wgpu::TextureViewDimension::e2D;
    viewDescriptor.mipLevelCount = 1;
    viewDescriptor.arrayLayerCount = 1;

    textureView = texture.CreateView(&viewDescriptor);
}

} // namespace

//----------------------------------------------------------------------
// Renderer Class implementation

void Renderer::Initialize(GLFWwindow *window, Camera *camera, Model *model, uint32_t width, uint32_t height,
                          const std::function<void()> &callback)
{
    m_window = window;
    m_camera = camera;
    m_model = model;
    m_width = width;
    m_height = height;

    m_instance = wgpu::CreateInstance();
    GetAdapter([this, callback](wgpu::Adapter adapter) {
        m_adapter = adapter;
        GetDevice([this, callback](wgpu::Device device) {
            m_device = device;

#if defined(__EMSCRIPTEN__)
            wgpu::SurfaceDescriptorFromCanvasHTMLSelector canvasDesc{};
            canvasDesc.selector = "#canvas";
            wgpu::SurfaceDescriptor surfaceDesc{.nextInChain = &canvasDesc};
            m_surface = m_instance.CreateSurface(&surfaceDesc);
#else
            m_surface = wgpu::glfw::CreateSurfaceForWindow(m_instance, m_window);
#endif

            InitGraphics();

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
    UpdateUniforms();

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
    pass.SetPipeline(m_pipeline);
    pass.SetBindGroup(0, m_globalBindGroup);
    pass.SetBindGroup(1, m_modelBindGroup);
    pass.SetVertexBuffer(0, m_vertexBuffer);
    pass.SetIndexBuffer(m_indexBuffer, wgpu::IndexFormat::Uint32);
    pass.DrawIndexed(static_cast<uint32_t>(m_model->GetIndices().size()));
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
    m_pipeline = nullptr;
    m_shaderModule = nullptr;

    CreateRenderPipeline();
}

void Renderer::InitGraphics()
{
    ConfigureSurface();
    CreateDepthTexture();

    CreateVertexBuffer();
    CreateIndexBuffer();
    CreateUniformBuffers();
    CreateTexturesAndSamplers();
    CreateRenderPipeline();
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

void Renderer::CreateVertexBuffer()
{
    const std::vector<Model::Vertex> &vertexData = m_model->GetVertices();

    wgpu::BufferDescriptor vertexBufferDesc{};
    vertexBufferDesc.size = vertexData.size() * sizeof(Model::Vertex);
    vertexBufferDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
    vertexBufferDesc.mappedAtCreation = true;

    m_vertexBuffer = m_device.CreateBuffer(&vertexBufferDesc);
    std::memcpy(m_vertexBuffer.GetMappedRange(), vertexData.data(), vertexData.size() * sizeof(Model::Vertex));
    m_vertexBuffer.Unmap();
}

void Renderer::CreateIndexBuffer()
{
    const std::vector<uint32_t> indexData = m_model->GetIndices();

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
    globalUniforms.viewMatrix = glm::mat4(1.0f);       // Initialize as identity
    globalUniforms.projectionMatrix = glm::mat4(1.0f); // Initialize as identity
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

void Renderer::CreateTexturesAndSamplers()
{
    if (m_model->GetMaterials().empty())
    {
        return;
    }

    // Get the first Material from the Model for now. TODO: Support multiple materials
    const Model::Material &material = m_model->GetMaterials().front();

    // Base Color Texture
    CreateTexture(m_model->GetTexture(material.m_baseColorTexture), m_device, m_baseColorTexture,
                  m_baseColorTextureView);

    // Metallic-Roughness
    CreateTexture(m_model->GetTexture(material.m_metallicRoughnessTexture), m_device, m_metallicRoughnessTexture,
                  m_metallicRoughnessTextureView);

    // Normal Texture
    CreateTexture(m_model->GetTexture(material.m_normalTexture), m_device, m_normalTexture, m_normalTextureView);

    // Occlusion Texture
    CreateTexture(m_model->GetTexture(material.m_occlusionTexture), m_device, m_occlusionTexture,
                  m_occlusionTextureView);

    // Emissive Texture
    CreateTexture(m_model->GetTexture(material.m_emissiveTexture), m_device, m_emissiveTexture,
                  m_emissiveTextureView);

    // Create a sampler
    wgpu::SamplerDescriptor samplerDescriptor{};
    samplerDescriptor.addressModeU = wgpu::AddressMode::Repeat;
    samplerDescriptor.addressModeV = wgpu::AddressMode::Repeat;
    samplerDescriptor.addressModeW = wgpu::AddressMode::Repeat;
    samplerDescriptor.minFilter = wgpu::FilterMode::Linear;
    samplerDescriptor.magFilter = wgpu::FilterMode::Linear;
    samplerDescriptor.mipmapFilter = wgpu::MipmapFilterMode::Nearest; // wgpu::MipmapFilterMode::Linear;

    wgpu::Sampler sampler = m_device.CreateSampler(&samplerDescriptor);

    // Store the sampler for use with all textures
    m_sampler = sampler;

    std::cout << "Textures, texture views, and sampler created successfully." << std::endl;
}

void Renderer::CreateGlobalBindGroup()
{
    wgpu::BindGroupLayoutEntry bindGroupLayoutEntry{
        .binding = 0,
        .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
        .buffer = {.type = wgpu::BufferBindingType::Uniform,
                   .hasDynamicOffset = false,
                   .minBindingSize = sizeof(GlobalUniforms)},
    };

    wgpu::BindGroupLayoutDescriptor bindGroupLayoutDescriptor{
        .entryCount = 1,
        .entries = &bindGroupLayoutEntry,
    };

    m_globalBindGroupLayout = m_device.CreateBindGroupLayout(&bindGroupLayoutDescriptor);

    wgpu::BindGroupEntry bindGroupEntry{};
    bindGroupEntry.binding = 0;
    bindGroupEntry.buffer = m_globalUniformBuffer;
    bindGroupEntry.offset = 0;
    bindGroupEntry.size = sizeof(GlobalUniforms);

    wgpu::BindGroupDescriptor bindGroupDescriptor{};
    bindGroupDescriptor.layout = m_globalBindGroupLayout;
    bindGroupDescriptor.entryCount = 1;
    bindGroupDescriptor.entries = &bindGroupEntry;

    m_globalBindGroup = m_device.CreateBindGroup(&bindGroupDescriptor);
}

void Renderer::CreateModelBindGroup()
{
    wgpu::BindGroupLayoutEntry layoutEntries[7] = {
        {
            .binding = 0,
            .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
            .buffer = {.type = wgpu::BufferBindingType::Uniform,
                       .hasDynamicOffset = false,
                       .minBindingSize = sizeof(ModelUniforms)},
        },
        {
            // Sampler binding
            .binding = 1,
            .visibility = wgpu::ShaderStage::Fragment,
            .sampler = {.type = wgpu::SamplerBindingType::Filtering},
        },
        {
            // Base color texture binding
            .binding = 2,
            .visibility = wgpu::ShaderStage::Fragment,
            .texture = {.sampleType = wgpu::TextureSampleType::Float,
                        .viewDimension = wgpu::TextureViewDimension::e2D,
                        .multisampled = false},
        },
        {
            // Metallic-Roughness texture binding
            .binding = 3,
            .visibility = wgpu::ShaderStage::Fragment,
            .texture = {.sampleType = wgpu::TextureSampleType::Float,
                        .viewDimension = wgpu::TextureViewDimension::e2D,
                        .multisampled = false},
        },
        {
            // Normal texture binding
            .binding = 4,
            .visibility = wgpu::ShaderStage::Fragment,
            .texture = {.sampleType = wgpu::TextureSampleType::Float,
                        .viewDimension = wgpu::TextureViewDimension::e2D,
                        .multisampled = false},
        },
        {
            // Occlusion texture binding
            .binding = 5,
            .visibility = wgpu::ShaderStage::Fragment,
            .texture = {.sampleType = wgpu::TextureSampleType::Float,
                        .viewDimension = wgpu::TextureViewDimension::e2D,
                        .multisampled = false},
        },
        {
            // Emissive texture binding
            .binding = 6,
            .visibility = wgpu::ShaderStage::Fragment,
            .texture = {.sampleType = wgpu::TextureSampleType::Float,
                        .viewDimension = wgpu::TextureViewDimension::e2D,
                        .multisampled = false},
        },

    };

    wgpu::BindGroupLayoutDescriptor bindGroupLayoutDescriptor{
        .entryCount = 7,
        .entries = layoutEntries,
    };

    m_modelBindGroupLayout = m_device.CreateBindGroupLayout(&bindGroupLayoutDescriptor);

    wgpu::BindGroupEntry bindGroupEntries[7] = {{
                                                    .binding = 0,
                                                    .buffer = m_modelUniformBuffer,
                                                    .offset = 0,
                                                    .size = sizeof(ModelUniforms),
                                                },
                                                {
                                                    .binding = 1,
                                                    .sampler = m_sampler,
                                                },
                                                {
                                                    .binding = 2,
                                                    .textureView = m_baseColorTextureView,
                                                },
                                                {
                                                    .binding = 3,
                                                    .textureView = m_metallicRoughnessTextureView,
                                                },
                                                {
                                                    .binding = 4,
                                                    .textureView = m_normalTextureView,
                                                },
                                                {
                                                    .binding = 5,
                                                    .textureView = m_occlusionTextureView,
                                                },
                                                {
                                                    .binding = 6,
                                                    .textureView = m_emissiveTextureView,
                                                }};

    wgpu::BindGroupDescriptor bindGroupDescriptor{
        .layout = m_modelBindGroupLayout,
        .entryCount = 7,
        .entries = bindGroupEntries,
    };

    m_modelBindGroup = m_device.CreateBindGroup(&bindGroupDescriptor);
}

void Renderer::CreateRenderPipeline()
{
    wgpu::ShaderModuleWGSLDescriptor wgslDesc{};
    const std::string shader = LoadShaderFile("./assets/shaders/basic.wgsl");
    wgslDesc.code = shader.c_str();

    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{.nextInChain = &wgslDesc};
    m_shaderModule = m_device.CreateShaderModule(&shaderModuleDescriptor);

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
        .module = m_shaderModule, .entryPoint = "fragmentMain", .targetCount = 1, .targets = &colorTargetState};

    wgpu::DepthStencilState depthStencilState{
        .format = wgpu::TextureFormat::Depth24PlusStencil8,
        .depthWriteEnabled = true,
        .depthCompare = wgpu::CompareFunction::Less,
    };

    CreateGlobalBindGroup();
    CreateModelBindGroup();

    wgpu::BindGroupLayout bindGroupLayouts[] = {m_globalBindGroupLayout, m_modelBindGroupLayout};

    wgpu::PipelineLayoutDescriptor layoutDescriptor{
        .bindGroupLayoutCount = 2,
        .bindGroupLayouts = bindGroupLayouts,
    };

    wgpu::PipelineLayout pipelineLayout = m_device.CreatePipelineLayout(&layoutDescriptor);

    wgpu::RenderPipelineDescriptor descriptor{.layout = pipelineLayout,
                                              .vertex =
                                                  {
                                                      .module = m_shaderModule,
                                                      .entryPoint = "vertexMain",
                                                      .bufferCount = 1,
                                                      .buffers = &vertexBufferLayout,
                                                  },
                                              .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
                                              .depthStencil = &depthStencilState,
                                              .fragment = &fragmentState};

    m_pipeline = m_device.CreateRenderPipeline(&descriptor);
}

void Renderer::UpdateUniforms() const
{
    // Update the global uniforms
    GlobalUniforms globalUniforms;
    globalUniforms.viewMatrix = m_camera->GetViewMatrix();
    globalUniforms.projectionMatrix = m_camera->GetProjectionMatrix();
    globalUniforms.cameraPosition = m_camera->GetWorldPosition();

    // Upload the uniforms to the GPU
    m_device.GetQueue().WriteBuffer(m_globalUniformBuffer, 0, &globalUniforms, sizeof(GlobalUniforms));

    // Update the model uniforms
    ModelUniforms modelUniforms;
    modelUniforms.modelMatrix = m_model->GetTransform();

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