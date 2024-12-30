

struct Uniforms {
    viewMatrix: mat4x4<f32>,
    projectionMatrix: mat4x4<f32>,
    modelMatrix: mat4x4<f32>
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

struct VertexOutput {
    @builtin(position) position: vec4<f32>,
    @location(0) fragColor: vec3<f32>
};

@vertex
fn vertexMain(
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) color: vec3<f32>
) -> VertexOutput {
    // Apply model, view, and projection transformations
    let worldPosition = uniforms.modelMatrix * vec4<f32>(position, 1.0);
    let viewPosition = uniforms.viewMatrix * worldPosition;
    let clipPosition = uniforms.projectionMatrix * viewPosition;

    var output: VertexOutput;
    output.position = clipPosition;
    output.fragColor = normal * 0.5 + 0.5; // Convert normal to color
    return output;
}

@fragment
fn fragmentMain(
    @location(0) fragColor: vec3<f32>  // Input: interpolated color from the vertex shader
) -> @location(0) vec4<f32> {
    return vec4<f32>(fragColor, 1.0); // Output the color with full opacity
}