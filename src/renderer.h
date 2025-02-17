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
class Environment;
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
    void Initialize(GLFWwindow *window, Camera *camera, Environment *environment, Model *model, uint32_t width,
                    uint32_t height, const std::function<void()> &callback);
    void Resize(uint32_t width, uint32_t height);
    void Render();
    void ReloadShaders();

  private:
    // Private utility methods
    void InitGraphics();
    void ConfigureSurface();
    void CreateDepthTexture();
    void CreateVertexBuffer();
    void CreateIndexBuffer();
    void CreateUniformBuffers();
    void CreateTexturesAndSamplers();
    void CreateGlobalBindGroup();
    void CreateModelBindGroup();
    void CreateRenderPipelines();
    void UpdateUniforms() const;
    void GetAdapter(const std::function<void(wgpu::Adapter)> &callback);
    void GetDevice(const std::function<void(wgpu::Device)> &callback);
    std::string LoadShaderFile(const std::string &filepath) const;

    // Types
    struct GlobalUniforms
    {
        alignas(16) glm::mat4 viewMatrix;
        alignas(16) glm::mat4 projectionMatrix;
        alignas(16) glm::mat4 inverseViewMatrix;
        alignas(16) glm::mat4 inverseProjectionMatrix;
        alignas(16) glm::vec3 cameraPosition;
        alignas(16) float padding[1];
    };

    struct ModelUniforms
    {
        alignas(16) glm::mat4 modelMatrix;
        alignas(16) glm::mat4 normalMatrix;
    };

    // Private member variables
    GLFWwindow *m_window = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    Camera *m_camera = nullptr;
    Environment *m_environment = nullptr;
    Model *m_model = nullptr;

    // WebGPU variables
    wgpu::Instance m_instance;
    wgpu::Adapter m_adapter;
    wgpu::Device m_device;
    wgpu::Surface m_surface;
    wgpu::TextureFormat m_surfaceFormat;
    wgpu::Texture m_depthTexture;
    wgpu::TextureView m_depthTextureView;

    // Global data
    wgpu::Buffer m_globalUniformBuffer;
    wgpu::BindGroupLayout m_globalBindGroupLayout;
    wgpu::BindGroup m_globalBindGroup;

    // Environment and IBL related data
    wgpu::Texture m_environmentTexture;
    wgpu::TextureView m_environmentTextureView;
    wgpu::Texture m_iblIrradianceTexture;
    wgpu::TextureView m_iblIrradianceTextureView;
    wgpu::Texture m_iblSpecularTexture;
    wgpu::TextureView m_iblSpecularTextureView;
    wgpu::Texture m_iblBrdfIntegrationLUT;
    wgpu::TextureView m_iblBrdfIntegrationLUTView;
    wgpu::Sampler m_environmentCubeSampler;
    wgpu::Sampler m_iblBrdfIntegrationLUTSampler;
    wgpu::ShaderModule m_environmentShaderModule;
    wgpu::RenderPipeline m_environmentPipeline;

    // Model related data. TODO: Move to separate class
    wgpu::ShaderModule m_modelShaderModule;
    wgpu::BindGroupLayout m_modelBindGroupLayout;
    wgpu::BindGroup m_modelBindGroup;
    wgpu::RenderPipeline m_modelPipeline;
    wgpu::Buffer m_vertexBuffer;
    wgpu::Buffer m_indexBuffer;
    wgpu::Buffer m_modelUniformBuffer;
    wgpu::Sampler m_sampler;

    wgpu::Texture m_baseColorTexture;
    wgpu::TextureView m_baseColorTextureView;
    wgpu::Texture m_metallicRoughnessTexture;
    wgpu::TextureView m_metallicRoughnessTextureView;
    wgpu::Texture m_normalTexture;
    wgpu::TextureView m_normalTextureView;
    wgpu::Texture m_occlusionTexture;
    wgpu::TextureView m_occlusionTextureView;
    wgpu::Texture m_emissiveTexture;
    wgpu::TextureView m_emissiveTextureView;
    wgpu::TextureView m_textureView;
};
