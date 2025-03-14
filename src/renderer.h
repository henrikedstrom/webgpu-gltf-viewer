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
    void Initialize(GLFWwindow *window, Camera *camera, Environment *environment, const Model &model, uint32_t width,
                    uint32_t height, const std::function<void()> &callback);
    void Resize(uint32_t width, uint32_t height);
    void Render(const glm::mat4 &modelMatrix);
    void ReloadShaders();
    void UpdateModel(const Model &model);
    void UpdateEnvironment(const Environment &environment);

  private:
    // Private utility methods
    void InitGraphics(const Model &model);
    void ConfigureSurface();
    void CreateDepthTexture();
    void CreateBindGroupLayouts();
    void CreateVertexBuffer(const Model &model);
    void CreateIndexBuffer(const Model &model);
    void CreateUniformBuffers();
    void CreateEnvironmentTexturesAndSamplers();
    void CreateSubMeshes(const Model &model);
    void CreateMaterials(const Model &model);
    void CreateGlobalBindGroup();
    void CreateEnvironmentRenderPipeline();
    void CreateModelRenderPipeline();
    void UpdateUniforms(const glm::mat4 &modelMatrix) const;
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

    struct MaterialUniforms
    {
        alignas(16) glm::vec4 baseColorFactor;
        alignas(16) glm::vec3 emissiveFactor;
        alignas(4) float metallicFactor;
        alignas(4) float roughnessFactor;
        alignas(4) float normalScale;
        alignas(4) float occlusionStrength;
        alignas(4) float alphaCutoff; // Used for Mask mode
        alignas(4) int alphaMode;     // 0 = Opaque, 1 = Mask, 2 = Blend
    };

    struct Material {
      MaterialUniforms m_uniforms;
      wgpu::Buffer m_uniformBuffer;
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
      wgpu::BindGroup m_bindGroup;
    };

    struct SubMesh {
      uint32_t m_firstIndex = 0; // First index in the index buffer
      uint32_t m_indexCount = 0; // Number of indices in the submesh
      int m_materialIndex = -1;  // Material index for the submesh
    };

    // Private member variables
    GLFWwindow *m_window = nullptr;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    Camera *m_camera = nullptr;
    Environment *m_environment = nullptr;

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
    wgpu::RenderPipeline m_modelPipeline;
    wgpu::Buffer m_vertexBuffer;
    wgpu::Buffer m_indexBuffer;
    wgpu::Buffer m_modelUniformBuffer;
    wgpu::Sampler m_sampler;

    std::vector<SubMesh> m_subMeshes;
    std::vector<Material> m_materials;
};
