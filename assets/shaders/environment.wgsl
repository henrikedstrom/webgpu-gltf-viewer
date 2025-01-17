
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
// Bind Groups

@group(0) @binding(0) var<uniform> globalUniforms: GlobalUniforms;
@group(0) @binding(1) var environmentSampler: sampler;
@group(0) @binding(2) var environmentTexture: texture_2d<f32>;
@group(0) @binding(3) var environmentIrradianceTexture: texture_2d<f32>;


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
    dir = invRotMatrix * dir;

    // Convert the direction vector to spherical coordinates
    let theta = -acos(dir.y); // Polar angle
    let phi = atan2(dir.z, dir.x); // Azimuthal angle

    // Convert to UV coordinates
    var iblUv = vec2f(phi / (2.0 * pi), 1.0 - theta / pi);

    // Sample the environment texture
    let iblSample = textureSample(environmentTexture, environmentSampler, iblUv).rgb;

    return vec4f(iblSample, 1.0);
}
