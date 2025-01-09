struct GlobalUniforms {
    viewMatrix: mat4x4<f32>,
    projectionMatrix: mat4x4<f32>
};

struct ModelUniforms {
    modelMatrix: mat4x4<f32>,
    normalMatrix: mat4x4<f32>
};

@group(0) @binding(0) var<uniform> globalUniforms: GlobalUniforms;
@group(1) @binding(0) var<uniform> modelUniforms: ModelUniforms;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,  // Clip-space position
    @location(0) fragColor: vec4<f32>,       // Vertex color
    @location(1) fragTexCoord0: vec2<f32>,   // Texture coordinate 0
    @location(2) fragTexCoord1: vec2<f32>,   // Texture coordinate 1
    @location(3) fragNormalWorld: vec3<f32>  // Normal vector (in World Space)
};

@vertex
fn vertexMain(
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) tangent: vec4<f32>,
    @location(3) texCoord0: vec2<f32>,
    @location(4) texCoord1: vec2<f32>,
    @location(5) color: vec4<f32>
) -> VertexOutput {
    // Transform the position to world space
    let worldPosition = modelUniforms.modelMatrix * vec4<f32>(position, 1.0);

    // Transform the normal to world space using the normal matrix (3x3 inverse transpose)
    let worldNormal = (modelUniforms.normalMatrix * vec4f(normal, 0.0)).xyz;

    var output: VertexOutput;
    output.position = globalUniforms.projectionMatrix * globalUniforms.viewMatrix * worldPosition;
    output.fragColor = color;
    output.fragTexCoord0 = texCoord0;
    output.fragTexCoord1 = texCoord1;
    output.fragNormalWorld = worldNormal; // Pass the world-space normal to the fragment shader
    return output;
}

@fragment
fn fragmentMain(
    @location(0) fragColor: vec4<f32>,      // Interpolated vertex color
    @location(1) fragTexCoord0: vec2<f32>,  // Interpolated texture coordinate 0
    @location(2) fragTexCoord1: vec2<f32>,  // Interpolated texture coordinate 1
    @location(3) fragNormalWorld: vec3<f32> // Interpolated normal vector
) -> @location(0) vec4<f32> {
    // Define a global light direction in world space
    let globalLightDirWorld: vec3<f32> = normalize(vec3<f32>(1.0, 1.0, -1.0));

    // Define light color
    let lightColor: vec3<f32> = vec3<f32>(1.0, 0.9, 0.7); // Slight yellow tint

    // Normalize the interpolated normal vector
    let normalizedNormal = normalize(fragNormalWorld);

    // Perform diffuse lighting calculation in world space
    let diffuseLighting: f32 = max(dot(normalizedNormal, globalLightDirWorld), 0.0);

    // Modulate the vertex color by the diffuse lighting and light color
    var litColor: vec3<f32> = fragColor.rgb * diffuseLighting * lightColor;

    // Final output
    return vec4<f32>(litColor, fragColor.a);
}
