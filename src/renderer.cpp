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
#include <webgpu/webgpu_cpp.h>

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
// Internal Utility Functions and Variables - [TODO] All to be refactored

namespace
{

wgpu::Instance instance;
wgpu::Adapter adapter;
wgpu::Device device;
wgpu::RenderPipeline pipeline;
wgpu::Buffer vertexBuffer;
wgpu::Buffer indexBuffer;
wgpu::Buffer uniformBuffer;
wgpu::BindGroup bindGroup;
wgpu::Surface surface;
wgpu::TextureFormat format;
wgpu::Texture depthTexture;
wgpu::TextureView depthTextureView;

struct Uniforms
{
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
    glm::mat4 modelMatrix;
};

Uniforms uniforms;

//----------------------------------------------------------------------
// Utility functions

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

//----------------------------------------------------------------------

void ConfigureSurface(uint32_t width, uint32_t height)
{
    wgpu::SurfaceCapabilities capabilities;
    surface.GetCapabilities(adapter, &capabilities);
    format = capabilities.formats[0];

    wgpu::SurfaceConfiguration config{.device = device, .format = format, .width = width, .height = height};
    surface.Configure(&config);
}

void CreateVertexBuffer(const std::vector<float> &vertexData)
{
    wgpu::BufferDescriptor vertexBufferDesc{};
    vertexBufferDesc.size = vertexData.size() * sizeof(float);
    vertexBufferDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
    vertexBufferDesc.mappedAtCreation = true;

    vertexBuffer = device.CreateBuffer(&vertexBufferDesc);
    std::memcpy(vertexBuffer.GetMappedRange(), vertexData.data(), vertexData.size() * sizeof(float));
    vertexBuffer.Unmap();
}

void CreateIndexBuffer(const std::vector<uint32_t> &indexData)
{
    wgpu::BufferDescriptor indexBufferDesc{};
    indexBufferDesc.size = indexData.size() * sizeof(uint32_t);
    indexBufferDesc.usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;
    indexBufferDesc.mappedAtCreation = true;

    indexBuffer = device.CreateBuffer(&indexBufferDesc);
    std::memcpy(indexBuffer.GetMappedRange(), indexData.data(), indexData.size() * sizeof(uint32_t));
    indexBuffer.Unmap();
}

void CreateUniformBuffer()
{
    wgpu::BufferDescriptor bufferDescriptor{};
    bufferDescriptor.size = sizeof(Uniforms);
    bufferDescriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;

    uniformBuffer = device.CreateBuffer(&bufferDescriptor);

    // Initialize the uniforms with default values
    uniforms.viewMatrix = glm::mat4(1.0f);       // Identity matrix
    uniforms.projectionMatrix = glm::mat4(1.0f); // Identity matrix
    uniforms.modelMatrix = glm::mat4(1.0f);      // Identity matrix

    device.GetQueue().WriteBuffer(uniformBuffer, 0, &uniforms, sizeof(Uniforms));
}

void CreateBindGroup(wgpu::BindGroupLayout bindGroupLayout)
{
    wgpu::BindGroupEntry bindGroupEntry{};
    bindGroupEntry.binding = 0;
    bindGroupEntry.buffer = uniformBuffer;
    bindGroupEntry.offset = 0;
    bindGroupEntry.size = sizeof(Uniforms);

    wgpu::BindGroupDescriptor bindGroupDescriptor{};
    bindGroupDescriptor.layout = bindGroupLayout;
    bindGroupDescriptor.entryCount = 1;
    bindGroupDescriptor.entries = &bindGroupEntry;

    bindGroup = device.CreateBindGroup(&bindGroupDescriptor);
}

void CreateDepthTexture(uint32_t width, uint32_t height)
{
    wgpu::TextureDescriptor depthTextureDescriptor{};
    depthTextureDescriptor.size = {width, height, 1};
    depthTextureDescriptor.format = wgpu::TextureFormat::Depth24PlusStencil8;
    depthTextureDescriptor.usage = wgpu::TextureUsage::RenderAttachment;

    depthTexture = device.CreateTexture(&depthTextureDescriptor);
    depthTextureView = depthTexture.CreateView();
}

void CreateRenderPipeline()
{
    wgpu::ShaderModuleWGSLDescriptor wgslDesc{};
    const std::string shader = LoadShaderFile("./assets/shaders/basic.wgsl");
    wgslDesc.code = shader.c_str();

    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{.nextInChain = &wgslDesc};
    wgpu::ShaderModule shaderModule = device.CreateShaderModule(&shaderModuleDescriptor);

    wgpu::VertexAttribute vertexAttributes[] = {
        {.format = wgpu::VertexFormat::Float32x3, .offset = 0, .shaderLocation = 0},
        {.format = wgpu::VertexFormat::Float32x3, .offset = 3 * sizeof(float), .shaderLocation = 1},
        {.format = wgpu::VertexFormat::Float32x3, .offset = 6 * sizeof(float), .shaderLocation = 2}};

    wgpu::VertexBufferLayout vertexBufferLayout{.arrayStride = 9 * sizeof(float),
                                                .stepMode = wgpu::VertexStepMode::Vertex,
                                                .attributeCount = 3,
                                                .attributes = vertexAttributes};

    wgpu::ColorTargetState colorTargetState{.format = format};

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

    wgpu::BindGroupLayout bindGroupLayout = device.CreateBindGroupLayout(&bindGroupLayoutDescriptor);

    // Step 2: Create the bind group
    CreateBindGroup(bindGroupLayout);

    // Step 3: Create the pipeline layout using the bind group layout
    wgpu::PipelineLayoutDescriptor layoutDescriptor{
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &bindGroupLayout,
    };

    wgpu::PipelineLayout pipelineLayout = device.CreatePipelineLayout(&layoutDescriptor);

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

    pipeline = device.CreateRenderPipeline(&descriptor);
}

void UpdateUniforms(const Camera &camera, const Model &model)
{
    uniforms.modelMatrix = model.GetTransform();
    uniforms.viewMatrix = camera.GetViewMatrix();
    uniforms.projectionMatrix = camera.GetProjectionMatrix();

    device.GetQueue().WriteBuffer(uniformBuffer, 0, &uniforms, sizeof(Uniforms));
}

void InitGraphics(uint32_t width, uint32_t height, Model *model)
{
    ConfigureSurface(width, height);
    CreateVertexBuffer(model->GetVertices());
    CreateIndexBuffer(model->GetIndices());
    CreateUniformBuffer();
    CreateDepthTexture(width, height);
    CreateRenderPipeline();
}

void GetAdapter(const std::function<void(wgpu::Adapter)> &callback)
{
    instance.RequestAdapter(
        nullptr,
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

void GetDevice(const std::function<void(wgpu::Device)> &callback)
{
    adapter.RequestDevice(
        nullptr,
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

    instance = wgpu::CreateInstance();
    GetAdapter([window, width, height, model, callback](wgpu::Adapter a) {
        adapter = a;
        GetDevice([window, width, height, model, callback](wgpu::Device d) {
            device = d;

#if defined(__EMSCRIPTEN__)
            wgpu::SurfaceDescriptorFromCanvasHTMLSelector canvasDesc{};
            canvasDesc.selector = "#canvas";

            wgpu::SurfaceDescriptor surfaceDesc{.nextInChain = &canvasDesc};
            surface = instance.CreateSurface(&surfaceDesc);
#else
            surface = wgpu::glfw::CreateSurfaceForWindow(instance, window);
#endif

            InitGraphics(width, height, model);

            // Invoke the callback
            callback();
        });
    });
}

void Renderer::Render() const
{
    // Update camera and model transformations
    UpdateUniforms(*m_camera, *m_model);

    wgpu::SurfaceTexture surfaceTexture;
    surface.GetCurrentTexture(&surfaceTexture);

    wgpu::RenderPassColorAttachment attachment{.view = surfaceTexture.texture.CreateView(),
                                               .loadOp = wgpu::LoadOp::Clear,
                                               .storeOp = wgpu::StoreOp::Store,
                                               .clearValue = {.r = 0.0f, .g = 0.2f, .b = 0.4f, .a = 1.0f}};

    wgpu::RenderPassDepthStencilAttachment depthAttachment{
        .view = depthTextureView,
        .depthLoadOp = wgpu::LoadOp::Clear,
        .depthStoreOp = wgpu::StoreOp::Store,
        .depthClearValue = 1.0f, // Clear to the farthest value
        .stencilLoadOp = wgpu::LoadOp::Clear,
        .stencilStoreOp = wgpu::StoreOp::Store,
    };

    wgpu::RenderPassDescriptor renderpass{
        .colorAttachmentCount = 1, .colorAttachments = &attachment, .depthStencilAttachment = &depthAttachment};

    wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&renderpass);
    pass.SetPipeline(pipeline);
    pass.SetBindGroup(0, bindGroup);
    pass.SetVertexBuffer(0, vertexBuffer);
    pass.SetIndexBuffer(indexBuffer, wgpu::IndexFormat::Uint32);
    pass.DrawIndexed(m_model->GetIndices().size());
    pass.End();
    wgpu::CommandBuffer commands = encoder.Finish();
    device.GetQueue().Submit(1, &commands);

#if !defined(__EMSCRIPTEN__)
    surface.Present();
    instance.ProcessEvents();
#endif
}