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
    pass.SetBindGroup(0, m_bindGroup);
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

void Renderer::InitGraphics()
{
    ConfigureSurface();
    CreateVertexBuffer();
    CreateIndexBuffer();
    CreateUniformBuffer();
    CreateDepthTexture();
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

void Renderer::CreateVertexBuffer()
{
    const std::vector<float> vertexData = m_model->GetVertices();

    wgpu::BufferDescriptor vertexBufferDesc{};
    vertexBufferDesc.size = vertexData.size() * sizeof(float);
    vertexBufferDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
    vertexBufferDesc.mappedAtCreation = true;

    m_vertexBuffer = m_device.CreateBuffer(&vertexBufferDesc);
    std::memcpy(m_vertexBuffer.GetMappedRange(), vertexData.data(), vertexData.size() * sizeof(float));
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

void Renderer::CreateUniformBuffer()
{
    wgpu::BufferDescriptor bufferDescriptor{};
    bufferDescriptor.size = sizeof(Uniforms);
    bufferDescriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;

    m_uniformBuffer = m_device.CreateBuffer(&bufferDescriptor);

    // Initialize the uniforms with default values
    Uniforms uniforms;
    uniforms.viewMatrix = glm::mat4(1.0f);       // Identity matrix
    uniforms.projectionMatrix = glm::mat4(1.0f); // Identity matrix
    uniforms.modelMatrix = glm::mat4(1.0f);      // Identity matrix

    m_device.GetQueue().WriteBuffer(m_uniformBuffer, 0, &uniforms, sizeof(Uniforms));
}

void Renderer::CreateBindGroup(wgpu::BindGroupLayout bindGroupLayout)
{
    wgpu::BindGroupEntry bindGroupEntry{};
    bindGroupEntry.binding = 0;
    bindGroupEntry.buffer = m_uniformBuffer;
    bindGroupEntry.offset = 0;
    bindGroupEntry.size = sizeof(Uniforms);

    wgpu::BindGroupDescriptor bindGroupDescriptor{};
    bindGroupDescriptor.layout = bindGroupLayout;
    bindGroupDescriptor.entryCount = 1;
    bindGroupDescriptor.entries = &bindGroupEntry;

    m_bindGroup = m_device.CreateBindGroup(&bindGroupDescriptor);
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

void Renderer::CreateRenderPipeline()
{
    wgpu::ShaderModuleWGSLDescriptor wgslDesc{};
    const std::string shader = LoadShaderFile("./assets/shaders/basic.wgsl");
    wgslDesc.code = shader.c_str();

    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{.nextInChain = &wgslDesc};
    wgpu::ShaderModule shaderModule = m_device.CreateShaderModule(&shaderModuleDescriptor);

    wgpu::VertexAttribute vertexAttributes[] = {
        {.format = wgpu::VertexFormat::Float32x3, .offset = 0, .shaderLocation = 0},
        {.format = wgpu::VertexFormat::Float32x3, .offset = 3 * sizeof(float), .shaderLocation = 1},
        {.format = wgpu::VertexFormat::Float32x3, .offset = 6 * sizeof(float), .shaderLocation = 2}};

    wgpu::VertexBufferLayout vertexBufferLayout{.arrayStride = 9 * sizeof(float),
                                                .stepMode = wgpu::VertexStepMode::Vertex,
                                                .attributeCount = 3,
                                                .attributes = vertexAttributes};

    wgpu::ColorTargetState colorTargetState{.format = m_surfaceFormat};

    wgpu::FragmentState fragmentState{
        .module = shaderModule, .entryPoint = "fragmentMain", .targetCount = 1, .targets = &colorTargetState};

    wgpu::DepthStencilState depthStencilState{
        .format = wgpu::TextureFormat::Depth24PlusStencil8,
        .depthWriteEnabled = true,
        .depthCompare = wgpu::CompareFunction::Less,
    };

    // Step 1: Create an explicit bind group layout
    wgpu::BindGroupLayoutEntry bindGroupLayoutEntry{
        .binding = 0,
        .visibility = wgpu::ShaderStage::Vertex,
        .buffer = {.type = wgpu::BufferBindingType::Uniform,
                   .hasDynamicOffset = false,
                   .minBindingSize = sizeof(Uniforms)},
    };

    wgpu::BindGroupLayoutDescriptor bindGroupLayoutDescriptor{
        .entryCount = 1,
        .entries = &bindGroupLayoutEntry,
    };

    wgpu::BindGroupLayout bindGroupLayout = m_device.CreateBindGroupLayout(&bindGroupLayoutDescriptor);

    // Step 2: Create the bind group
    CreateBindGroup(bindGroupLayout);

    // Step 3: Create the pipeline layout using the bind group layout
    wgpu::PipelineLayoutDescriptor layoutDescriptor{
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &bindGroupLayout,
    };

    wgpu::PipelineLayout pipelineLayout = m_device.CreatePipelineLayout(&layoutDescriptor);

    wgpu::RenderPipelineDescriptor descriptor{.layout = pipelineLayout,
                                              .vertex =
                                                  {
                                                      .module = shaderModule,
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
    Uniforms uniforms;
    uniforms.viewMatrix = m_camera->GetViewMatrix();
    uniforms.projectionMatrix = m_camera->GetProjectionMatrix();
    uniforms.modelMatrix = m_model->GetTransform();

    m_device.GetQueue().WriteBuffer(m_uniformBuffer, 0, &uniforms, sizeof(Uniforms));
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
    deviceDesc.SetDeviceLostCallback(
        wgpu::CallbackMode::AllowSpontaneous,
        [](const wgpu::Device &device, wgpu::DeviceLostReason reason, const char *message) {
            std::cerr << "Device lost: ";

            // Print the reason
            switch (reason)
            {
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
            default:
                std::cerr << "[Reason: Unrecognized]";
                break;
            }

            // Print the message
            if (message)
            {
                std::cerr << " - " << message << std::endl;
            }
            else
            {
                std::cerr << " - No message provided." << std::endl;
            }
        });

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
    });

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