
//---------------------------------------------------------------------
// Uniforms

struct GlobalUniforms {
    viewMatrix: mat4x4<f32>,
    projectionMatrix: mat4x4<f32>,
    inverseViewMatrix: mat4x4<f32>,
    inverseProjectionMatrix: mat4x4<f32>,
    cameraPositionWorld: vec3<f32>
};


//---------------------------------------------------------------------
// Constants and Types

const pi = 3.141592653589793;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f
};


//---------------------------------------------------------------------
// Utility Functions

fn toneMapPBRNeutral(colorIn: vec3f) -> vec3f {
    let startCompression: f32 = 0.8 - 0.04;
    let desaturation: f32 = 0.15;

    let x: f32 = min(colorIn.r, min(colorIn.g, colorIn.b));
    let offset: f32 = select(0.04, x - 6.25 * x * x, x < 0.08);
    var color = colorIn - offset;

    let peak: f32 = max(color.r, max(color.g, color.b));
    if (peak < startCompression) {
        return color;
    }

    let d: f32 = 1.0 - startCompression;
    let newPeak: f32 = 1.0 - d * d / (peak + d - startCompression);
    color = color * (newPeak / peak);

    let g: f32 = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(color, newPeak * vec3f(1.0, 1.0, 1.0), g);
}

fn toneMap(colorIn: vec3f) -> vec3f {
  const gamma = 2.2;
  const invGamma = 1.0 / gamma;

  const exposure = 1.0;

  var color = colorIn * exposure;

  color = toneMapPBRNeutral(color);

  // Linear to sRGB
  color = pow(color, vec3f(invGamma));

  return color;
}

//---------------------------------------------------------------------
// Bind Groups

@group(0) @binding(0) var<uniform> globalUniforms: GlobalUniforms;
@group(0) @binding(1) var environmentCubeSampler: sampler;
@group(0) @binding(2) var environmentTexture: texture_cube<f32>;
@group(0) @binding(3) var iblIrradianceTexture: texture_cube<f32>;
@group(0) @binding(4) var iblSpecularTexture: texture_cube<f32>;
@group(0) @binding(5) var iblBRDFIntegrationLUTTexture: texture_2d<f32>;
@group(0) @binding(6) var iblBRDFIntegrationLUTSampler: sampler;

//---------------------------------------------------------------------
// Vertex Shader

@vertex
fn vertexMain(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var positions: array<vec2f, 6> = array<vec2f, 6>(
        vec2f(-1.0, -1.0),
        vec2f(1.0, -1.0),
        vec2f(1.0, 1.0),
        vec2f(1.0, 1.0),
        vec2f(-1.0, 1.0),
        vec2f(-1.0, -1.0)
    );

    var output: VertexOutput;
    output.position = vec4f(positions[vertexIndex], 0.0, 1.0);
    output.uv = positions[vertexIndex] * 0.5 + 0.5;
    return output;
}


//---------------------------------------------------------------------
// Fragment Shader

@fragment
fn fragmentMain(input: VertexOutput) -> @location(0) vec4f {

    // Convert the UV coordinates to NDC
    let ndc = input.uv * 2.0 - 1.0;

    // Convert the NDC coordinates to view space
    let viewSpacePos = globalUniforms.inverseProjectionMatrix * vec4f(ndc.xy, 1.0, 1.0);
    var dir = normalize(viewSpacePos.xyz);

    // Create a rotation matrix from the camera's view matrix
    let invRotMatrix = mat3x3f(
        globalUniforms.inverseViewMatrix[0].xyz,
        globalUniforms.inverseViewMatrix[1].xyz,
        globalUniforms.inverseViewMatrix[2].xyz
    );

    // Transform the direction vector from world space to camera space
    dir = normalize(invRotMatrix * dir);

    // Sample the environment texture
    let iblSample = textureSample(environmentTexture, environmentCubeSampler, dir).rgb;

    // Tonemapping and gamma correction
    let color = toneMap(iblSample);

    return vec4f(color, 1.0);
}
