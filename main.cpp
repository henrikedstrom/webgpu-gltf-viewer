#include <GLFW/glfw3.h>
#include <webgpu/webgpu_cpp.h>
#include <cmath>
#include <iostream>
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#else
#include <webgpu/webgpu_glfw.h>
#endif

wgpu::Instance instance;
wgpu::Adapter adapter;
wgpu::Device device;
wgpu::RenderPipeline pipeline;
wgpu::Buffer vertexBuffer;
wgpu::Buffer uniformBuffer;
wgpu::BindGroup bindGroup;

wgpu::Surface surface;
wgpu::TextureFormat format;
const uint32_t kWidth = 512;
const uint32_t kHeight = 512;
float rotationAngle = 0.0f;
bool isAnimating = true; // Animation starts active

const char shaderCode[] = R"(
  
  @group(0) @binding(0) var<uniform> transformationMatrix: mat4x4<f32>;

  struct VertexOutput {
    @builtin(position) position: vec4<f32>,  // Position of the vertex
    @location(0) fragColor: vec3<f32>      // Color passed to the fragment shader
  };

  @vertex
  fn vertexMain(
    @location(0) position: vec2<f32>,  // Input: position from the vertex buffer
    @location(1) color: vec3<f32>      // Input: color from the vertex buffer
  ) -> VertexOutput {

    // Convert position to vec3 to apply 4x4 matrix
    let transformedPosition = transformationMatrix * vec4<f32>(position, 0.0, 1.0);

    var output: VertexOutput;
    output.position = vec4<f32>(transformedPosition.xy, 0.0, 1.0);
    output.fragColor = color;
    return output;
  }

  @fragment
  fn fragmentMain(
    @location(0) fragColor: vec3<f32>  // Input: interpolated color from the vertex shader
  ) -> @location(0) vec4<f32> {
    return vec4<f32>(fragColor, 1.0); // Output the color with full opacity
  }
)";

const float vertexData[] = {
  // Position (x, y)   // Color (r, g, b)
   0.0f,  0.6667f,     1.0f, 0.0f, 0.0f,  // Top vertex: Red
  -0.5f, -0.3333f,     0.0f, 1.0f, 0.0f,  // Bottom-left vertex: Green
   0.5f, -0.3333f,     0.0f, 0.0f, 1.0f   // Bottom-right vertex: Blue
};

const float transformationMatrix[16] = {
  1.0f, 0.0f, 0.0f, 0.0f,
  0.0f, 1.0f, 0.0f, 0.0f,
  0.0f, 0.0f, 1.0f, 0.0f,
  0.0f, 0.0f, 0.0f, 1.0f,
};

void UpdateTransformationMatrix() {
  // Calculate the rotation matrix
  float cosTheta = std::cos(rotationAngle);
  float sinTheta = std::sin(rotationAngle);

  float rotationMatrix[16] = {
    cosTheta, -sinTheta, 0.0f, 0.0f,
    sinTheta,  cosTheta, 0.0f, 0.0f,
    0.0f,      0.0f,     1.0f, 0.0f,
    0.0f,      0.0f,     0.0f, 1.0f,
  };

  // Write the updated matrix to the uniform buffer
  device.GetQueue().WriteBuffer(uniformBuffer, 0, rotationMatrix, sizeof(rotationMatrix));
}

void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
    isAnimating = !isAnimating; // Toggle the animation state
  }
}


void ConfigureSurface() {
  wgpu::SurfaceCapabilities capabilities;
  surface.GetCapabilities(adapter, &capabilities);
  format = capabilities.formats[0];

  wgpu::SurfaceConfiguration config{
    .device = device,
    .format = format,
    .width = kWidth,
    .height = kHeight};
  surface.Configure(&config);
}

void GetAdapter(void (*callback)(wgpu::Adapter)) {
  instance.RequestAdapter(
      nullptr,
      // TODO(https://bugs.chromium.org/p/dawn/issues/detail?id=1892): Use
      // wgpu::RequestAdapterStatus and wgpu::Adapter.
      [](WGPURequestAdapterStatus status, WGPUAdapter cAdapter,
         const char* message, void* userdata) {
        if (message) {
          printf("RequestAdapter: %s\n", message);
        }
        if (status != WGPURequestAdapterStatus_Success) {
          exit(0);
        }
        wgpu::Adapter adapter = wgpu::Adapter::Acquire(cAdapter);
        reinterpret_cast<void (*)(wgpu::Adapter)>(userdata)(adapter);
  }, reinterpret_cast<void*>(callback));
}

void GetDevice(void (*callback)(wgpu::Device)) {
  adapter.RequestDevice(
      nullptr,
      // TODO(https://bugs.chromium.org/p/dawn/issues/detail?id=1892): Use
      // wgpu::RequestDeviceStatus and wgpu::Device.
      [](WGPURequestDeviceStatus status, WGPUDevice cDevice,
          const char* message, void* userdata) {
        if (message) {
          printf("RequestDevice: %s\n", message);
        }
        wgpu::Device device = wgpu::Device::Acquire(cDevice);
        device.SetUncapturedErrorCallback(
            [](WGPUErrorType type, const char* message, void* userdata) {
              std::cout << "Error: " << type << " - message: " << message;
            },
            nullptr);
        reinterpret_cast<void (*)(wgpu::Device)>(userdata)(device);
  }, reinterpret_cast<void*>(callback));
}

void CreateVertexBuffer() {
  // Create a buffer descriptor
  wgpu::BufferDescriptor bufferDescriptor{};
  bufferDescriptor.size = sizeof(vertexData);
  bufferDescriptor.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
  bufferDescriptor.mappedAtCreation = true;

  // Create the buffer
  vertexBuffer = device.CreateBuffer(&bufferDescriptor);

  // Map the buffer to write data
  void* mappedBuffer = vertexBuffer.GetMappedRange();
  std::memcpy(mappedBuffer, vertexData, sizeof(vertexData));
  vertexBuffer.Unmap();
}

void CreateUniformBuffer() {
  wgpu::BufferDescriptor bufferDescriptor{};
  bufferDescriptor.size = sizeof(transformationMatrix);
  bufferDescriptor.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;

  uniformBuffer = device.CreateBuffer(&bufferDescriptor);

  device.GetQueue().WriteBuffer(uniformBuffer, 0, transformationMatrix, sizeof(transformationMatrix));
}

void CreateBindGroup(wgpu::BindGroupLayout bindGroupLayout) {
  wgpu::BindGroupEntry bindGroupEntry{};
  bindGroupEntry.binding = 0;
  bindGroupEntry.buffer = uniformBuffer;
  bindGroupEntry.offset = 0;
  bindGroupEntry.size = sizeof(transformationMatrix);

  wgpu::BindGroupDescriptor bindGroupDescriptor{};
  bindGroupDescriptor.layout = bindGroupLayout;
  bindGroupDescriptor.entryCount = 1;
  bindGroupDescriptor.entries = &bindGroupEntry;

  bindGroup = device.CreateBindGroup(&bindGroupDescriptor);
}

void CreateRenderPipeline() {
  wgpu::ShaderModuleWGSLDescriptor wgslDesc{};
  wgslDesc.code = shaderCode;

  wgpu::ShaderModuleDescriptor shaderModuleDescriptor{
      .nextInChain = &wgslDesc};
  wgpu::ShaderModule shaderModule =
      device.CreateShaderModule(&shaderModuleDescriptor);

  wgpu::VertexAttribute vertexAttributes[] = {
    {.format = wgpu::VertexFormat::Float32x2, .offset = 0, .shaderLocation = 0},
    {.format = wgpu::VertexFormat::Float32x3, .offset = 2 * sizeof(float), .shaderLocation = 1},
  };

  wgpu::VertexBufferLayout vertexBufferLayout{
    .arrayStride = 5 * sizeof(float),
    .stepMode = wgpu::VertexStepMode::Vertex,
    .attributeCount = 2,
    .attributes = vertexAttributes 
  };

  wgpu::ColorTargetState colorTargetState{.format = format};

  wgpu::FragmentState fragmentState{
    .module = shaderModule,
    .entryPoint = "fragmentMain",
    .targetCount = 1,
    .targets = &colorTargetState
  };


  // Step 1: Create an explicit bind group layout
  wgpu::BindGroupLayoutEntry bindGroupLayoutEntry{
    .binding = 0,
    .visibility = wgpu::ShaderStage::Vertex,
    .buffer = {.type = wgpu::BufferBindingType::Uniform, .hasDynamicOffset = false, .minBindingSize = sizeof(transformationMatrix)},
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

  wgpu::RenderPipelineDescriptor descriptor{
    .layout = pipelineLayout,
    .vertex = {
      .module = shaderModule,
      .entryPoint = "vertexMain",
      .bufferCount = 1,
      .buffers = &vertexBufferLayout,     
    },
    .primitive = {.topology = wgpu::PrimitiveTopology::TriangleList},
    .fragment = &fragmentState
  };

  pipeline = device.CreateRenderPipeline(&descriptor);
}

void Render() {

  // Update the rotation angle only if animation is active
  if (isAnimating) {
    rotationAngle += 0.01f; // Increment the angle for smooth rotation
    if (rotationAngle > 2.0f * M_PI) {
      rotationAngle -= 2.0f * M_PI; // Keep the angle within [0, 2π]
    }

    // Update the transformation matrix
    UpdateTransformationMatrix();
  }

  wgpu::SurfaceTexture surfaceTexture;
  surface.GetCurrentTexture(&surfaceTexture);

  wgpu::RenderPassColorAttachment attachment{
    .view = surfaceTexture.texture.CreateView(),
    .loadOp = wgpu::LoadOp::Clear,
    .storeOp = wgpu::StoreOp::Store,
    .clearValue = {.r = 0.0f, .g = 0.2f, .b = 0.4f, .a = 1.0f}
  };

  wgpu::RenderPassDescriptor renderpass{
    .colorAttachmentCount = 1,
    .colorAttachments = &attachment
  };

  wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
  wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&renderpass);
  pass.SetPipeline(pipeline);
  pass.SetBindGroup(0, bindGroup);
  pass.SetVertexBuffer(0, vertexBuffer);
  pass.Draw(3);
  pass.End();
  wgpu::CommandBuffer commands = encoder.Finish();
  device.GetQueue().Submit(1, &commands);
}

void InitGraphics() {
  ConfigureSurface();
  CreateVertexBuffer();
  CreateUniformBuffer();
  CreateRenderPipeline();
}

void Start() {
  if (!glfwInit()) {
    return;
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window =
      glfwCreateWindow(kWidth, kHeight, "WebGPU window", nullptr, nullptr);

  // Set the mouse button callback
  glfwSetMouseButtonCallback(window, MouseButtonCallback);

#if defined(__EMSCRIPTEN__)
  wgpu::SurfaceDescriptorFromCanvasHTMLSelector canvasDesc{};
  canvasDesc.selector = "#canvas";

  wgpu::SurfaceDescriptor surfaceDesc{.nextInChain = &canvasDesc};
  surface = instance.CreateSurface(&surfaceDesc);
#else
  surface = wgpu::glfw::CreateSurfaceForWindow(instance, window);
#endif

  InitGraphics();

#if defined(__EMSCRIPTEN__)
  emscripten_set_main_loop(Render, 0, false);
#else
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    Render();
    surface.Present();
    instance.ProcessEvents();
  }
#endif
}

int main() {
  instance = wgpu::CreateInstance();
  GetAdapter([](wgpu::Adapter a) {
    adapter = a;
    GetDevice([](wgpu::Device d) {
      device = d;
      Start();
    });
  });
}
