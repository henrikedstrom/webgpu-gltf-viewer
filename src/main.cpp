#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include <webgpu/webgpu_cpp.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_RIGHT_HANDED
#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <glm/gtx/polar_coordinates.hpp>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "tiny_gltf.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#else
#include <webgpu/webgpu_glfw.h>
#endif

#include "camera.h"
#include "orbit_controls.h"

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

Camera camera;
std::unique_ptr<OrbitControls> controls;
const uint32_t kWidth = 512;
const uint32_t kHeight = 512;
float rotationAngle = 0.0f;
bool isAnimating = true; // Animation starts active
bool quitApp = false;

struct Uniforms
{
    glm::mat4 viewMatrix;
    glm::mat4 projectionMatrix;
    glm::mat4 modelMatrix;
};

Uniforms uniforms;

//----------------------------------------------------------------------
// Input Callbacks

void KeyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    static bool keyState[GLFW_KEY_LAST] = {false};

    if (key >= 0 && key < GLFW_KEY_LAST)
    {
        bool keyPressed = action == GLFW_PRESS && !keyState[key];
        bool keyReleased = action == GLFW_RELEASE && keyState[key];

        if (action == GLFW_PRESS)
        {
            keyState[key] = true;
        }
        else if (action == GLFW_RELEASE)
        {
            keyState[key] = false;
        }

        if (keyPressed)
        {
            if (key == GLFW_KEY_A)
            {
                isAnimating = !isAnimating; // Toggle the animation state
            }
            else if (key == GLFW_KEY_ESCAPE)
            {
                quitApp = true; // Quit the application
            }
        }
    }
}

//----------------------------------------------------------------------
// Model loading

std::vector<float> vertices;
std::vector<uint32_t> indices;

void ProcessMesh(const tinygltf::Model &model, const tinygltf::Mesh &mesh, std::vector<float> &vertices,
                 std::vector<uint32_t> &indices)
{
    for (const auto &primitive : mesh.primitives)
    {
        // Access vertex positions
        const auto &positionAccessor = model.accessors[primitive.attributes.find("POSITION")->second];
        const auto &positionBufferView = model.bufferViews[positionAccessor.bufferView];
        const auto &positionBuffer = model.buffers[positionBufferView.buffer];
        const float *positionData = reinterpret_cast<const float *>(
            positionBuffer.data.data() + positionBufferView.byteOffset + positionAccessor.byteOffset);

        // Optional: Access vertex normals
        const auto normalIter = primitive.attributes.find("NORMAL");
        const float *normalData = nullptr;
        if (normalIter != primitive.attributes.end())
        {
            const auto &normalAccessor = model.accessors[normalIter->second];
            const auto &normalBufferView = model.bufferViews[normalAccessor.bufferView];
            const auto &normalBuffer = model.buffers[normalBufferView.buffer];
            normalData = reinterpret_cast<const float *>(normalBuffer.data.data() + normalBufferView.byteOffset +
                                                         normalAccessor.byteOffset);
        }

        // Optional: Access vertex colors
        const auto colorIter = primitive.attributes.find("COLOR_0");
        const float *colorData = nullptr;
        if (colorIter != primitive.attributes.end())
        {
            const auto &colorAccessor = model.accessors[colorIter->second];
            const auto &colorBufferView = model.bufferViews[colorAccessor.bufferView];
            const auto &colorBuffer = model.buffers[colorBufferView.buffer];
            colorData = reinterpret_cast<const float *>(colorBuffer.data.data() + colorBufferView.byteOffset +
                                                        colorAccessor.byteOffset);
        }

        // Copy vertex data (positions, normals, colors)
        for (size_t i = 0; i < positionAccessor.count; ++i)
        {
            // Position
            vertices.push_back(positionData[i * 3 + 0]); // x
            vertices.push_back(positionData[i * 3 + 1]); // y
            vertices.push_back(positionData[i * 3 + 2]); // z

            // Normal (default to 0, 0, 1 if not provided)
            if (normalData)
            {
                vertices.push_back(normalData[i * 3 + 0]); // nx
                vertices.push_back(normalData[i * 3 + 1]); // ny
                vertices.push_back(normalData[i * 3 + 2]); // nz
            }
            else
            {
                vertices.push_back(0.0f); // nx
                vertices.push_back(0.0f); // ny
                vertices.push_back(1.0f); // nz
            }

            // Color (default to white if not provided)
            if (colorData)
            {
                vertices.push_back(colorData[i * 3 + 0]); // r
                vertices.push_back(colorData[i * 3 + 1]); // g
                vertices.push_back(colorData[i * 3 + 2]); // b
            }
            else
            {
                vertices.push_back(1.0f); // r
                vertices.push_back(1.0f); // g
                vertices.push_back(1.0f); // b
            }
        }

        // Access indices (if present)
        if (primitive.indices >= 0)
        {
            const auto &indexAccessor = model.accessors[primitive.indices];
            const auto &indexBufferView = model.bufferViews[indexAccessor.bufferView];
            const auto &indexBuffer = model.buffers[indexBufferView.buffer];
            const void *indexData = indexBuffer.data.data() + indexBufferView.byteOffset + indexAccessor.byteOffset;

            if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT)
            {
                const uint16_t *data = reinterpret_cast<const uint16_t *>(indexData);
                for (size_t i = 0; i < indexAccessor.count; ++i)
                {
                    indices.push_back(static_cast<uint32_t>(data[i]));
                }
            }
            else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT)
            {
                const uint32_t *data = reinterpret_cast<const uint32_t *>(indexData);
                indices.insert(indices.end(), data, data + indexAccessor.count);
            }
        }
    }
}

void LoadModel(const std::string &filename)
{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    bool result = loader.LoadASCIIFromFile(&model, &err, &warn, filename);

    if (result)
    {
        for (const auto &mesh : model.meshes)
        {
            ProcessMesh(model, mesh, vertices, indices);
        }
    }
    else
    {
        std::cerr << "Failed to load model: " << err << std::endl;
    }
}

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

void ConfigureSurface()
{
    wgpu::SurfaceCapabilities capabilities;
    surface.GetCapabilities(adapter, &capabilities);
    format = capabilities.formats[0];

    wgpu::SurfaceConfiguration config{.device = device, .format = format, .width = kWidth, .height = kHeight};
    surface.Configure(&config);
}

void GetAdapter(void (*callback)(wgpu::Adapter))
{
    instance.RequestAdapter(
        nullptr,
        // TODO(https://bugs.chromium.org/p/dawn/issues/detail?id=1892): Use
        // wgpu::RequestAdapterStatus and wgpu::Adapter.
        [](WGPURequestAdapterStatus status, WGPUAdapter cAdapter, const char *message, void *userdata) {
            if (message)
            {
                printf("RequestAdapter: %s\n", message);
            }
            if (status != WGPURequestAdapterStatus_Success)
            {
                exit(0);
            }
            wgpu::Adapter adapter = wgpu::Adapter::Acquire(cAdapter);
            reinterpret_cast<void (*)(wgpu::Adapter)>(userdata)(adapter);
        },
        reinterpret_cast<void *>(callback));
}

void GetDevice(void (*callback)(wgpu::Device))
{
    adapter.RequestDevice(
        nullptr,
        // TODO(https://bugs.chromium.org/p/dawn/issues/detail?id=1892): Use
        // wgpu::RequestDeviceStatus and wgpu::Device.
        [](WGPURequestDeviceStatus status, WGPUDevice cDevice, const char *message, void *userdata) {
            if (message)
            {
                printf("RequestDevice: %s\n", message);
            }
            wgpu::Device device = wgpu::Device::Acquire(cDevice);
            device.SetUncapturedErrorCallback(
                [](WGPUErrorType type, const char *message, void *userdata) {
                    std::cout << "Error: " << type << " - message: " << message;
                },
                nullptr);
            reinterpret_cast<void (*)(wgpu::Device)>(userdata)(device);
        },
        reinterpret_cast<void *>(callback));
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

void CreateDepthTexture()
{
    wgpu::TextureDescriptor depthTextureDescriptor{};
    depthTextureDescriptor.size = {kWidth, kHeight, 1};
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

void UpdateUniforms()
{
    // Rotation angle to correct orientation (90 degrees in radians for X-axis)
    float xAxisAngle = M_PI / 2.0f; // 90 degrees

    // Create the X-axis rotation matrix
    glm::mat4 xRotationMatrix = glm::rotate(glm::mat4(1.0f), xAxisAngle, glm::vec3(1.0f, 0.0f, 0.0f));

    // Create the Y-axis rotation matrix (dynamic rotation angle)
    glm::mat4 yRotationMatrix = glm::rotate(glm::mat4(1.0f), -rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));

    // Combine the rotations: apply X-axis rotation first, then Y-axis rotation
    uniforms.modelMatrix = yRotationMatrix * xRotationMatrix;

    // Update the view and projection matrices from the camera
    uniforms.viewMatrix = camera.GetViewMatrix();
    uniforms.projectionMatrix = camera.GetProjectionMatrix();

    // Write the updated uniforms to the GPU
    device.GetQueue().WriteBuffer(uniformBuffer, 0, &uniforms, sizeof(Uniforms));
}

void Render()
{

    // Update the rotation angle only if animation is active
    if (isAnimating)
    {
        rotationAngle += 0.01f; // Increment the angle for smooth rotation
        if (rotationAngle > 2.0f * M_PI)
        {
            rotationAngle -= 2.0f * M_PI; // Keep the angle within [0, 2π]
        }
    }

    // Update camera and model transformations
    UpdateUniforms();

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
    pass.DrawIndexed(indices.size());
    pass.End();
    wgpu::CommandBuffer commands = encoder.Finish();
    device.GetQueue().Submit(1, &commands);
}

void InitGraphics()
{
    ConfigureSurface();
    CreateVertexBuffer(vertices);
    CreateIndexBuffer(indices);
    CreateUniformBuffer();
    CreateDepthTexture();
    CreateRenderPipeline();
}

void Start()
{
    if (!glfwInit())
    {
        return;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow *window = glfwCreateWindow(kWidth, kHeight, "WebGPU window", nullptr, nullptr);

    // Setup mouse and keyboard input callbacks
    controls = std::make_unique<OrbitControls>(window, &camera);
    glfwSetKeyCallback(window, KeyCallback);

#if defined(__EMSCRIPTEN__)
    wgpu::SurfaceDescriptorFromCanvasHTMLSelector canvasDesc{};
    canvasDesc.selector = "#canvas";

    wgpu::SurfaceDescriptor surfaceDesc{.nextInChain = &canvasDesc};
    surface = instance.CreateSurface(&surfaceDesc);
#else
    surface = wgpu::glfw::CreateSurfaceForWindow(instance, window);
#endif

    camera.ResizeViewport(kWidth, kHeight);

    LoadModel("./assets/models/DamagedHelmet/DamagedHelmet.gltf");
    InitGraphics();

    float lastFrameTime = 0.0f;

#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(Render, 0, false);
#else
    while (!glfwWindowShouldClose(window) && !quitApp)
    {
        float currentFrameTime = glfwGetTime();
        float deltaTime = currentFrameTime - lastFrameTime;
        lastFrameTime = currentFrameTime;

        glfwPollEvents();

        Render();
        surface.Present();
        instance.ProcessEvents();
    }

    // Cleanup (native only)
    glfwDestroyWindow(window);
    glfwTerminate();
#endif
}

int main()
{
    instance = wgpu::CreateInstance();
    GetAdapter([](wgpu::Adapter a) {
        adapter = a;
        GetDevice([](wgpu::Device d) {
            device = d;
            Start();
        });
    });
}
