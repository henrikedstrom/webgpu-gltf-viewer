#pragma once

// Standard Library Headers
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Third-Party Library Headers
#include <glm/glm.hpp>
#include <webgpu/webgpu_cpp.h>

// Forward Declarations
class Camera;
class Model;
struct GLFWwindow;

// Renderer Class
class Renderer
{
  public:
    // Constructor
    Renderer() = default;

    // Rule of 5
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    Renderer(Renderer &&) = delete;
    Renderer &operator=(Renderer &&) = delete;

    // Public Interface
    void Initialize(GLFWwindow *window, Camera *camera, Model *model, uint32_t width, uint32_t height,
                    const std::function<void()> &callback);
    void Render();
    void ReloadShaders();

  private:
    // Private utility methods
    void InitGraphics();
    void ConfigureSurface();
    void CreateVertexBuffer();
    void CreateIndexBuffer();
    void CreateUniformBuffer();
    void CreateBindGroup(wgpu::BindGroupLayout bindGroupLayout);
    void CreateDepthTexture();
    void CreateRenderPipeline();
    void UpdateUniforms() const;
    void GetAdapter(const std::function<void(wgpu::Adapter)> &callback);
    void GetDevice(const std::function<void(wgpu::Device)> &callback);
    std::string LoadShaderFile(const std::string &filepath) const;

    // Types
    struct Uniforms
    {
        glm::mat4 viewMatrix;
        glm::mat4 projectionMatrix;
        glm::mat4 modelMatrix;
        glm::mat4 normalMatrix;
    };

    // Private member variables
    GLFWwindow *m_window = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    Camera *m_camera = nullptr;
    Model *m_model = nullptr;

    // WebGPU variables
    wgpu::Instance m_instance;
    wgpu::Adapter m_adapter;
    wgpu::Device m_device;
    wgpu::Surface m_surface;
    wgpu::TextureFormat m_surfaceFormat;
    wgpu::ShaderModule m_shaderModule;
    wgpu::RenderPipeline m_pipeline;
    wgpu::Buffer m_vertexBuffer;
    wgpu::Buffer m_indexBuffer;
    wgpu::Buffer m_uniformBuffer;
    wgpu::BindGroup m_bindGroup;
    wgpu::Texture m_depthTexture;
    wgpu::TextureView m_depthTextureView;
};
