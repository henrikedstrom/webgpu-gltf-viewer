//=========================================================
// 2D normal-map mip generator (compute path)
// - previousMipLevel: L-1 source normals encoded in [0,1] (texture_2d<f32>)
// - nextMipLevel: target storage texture for mip L (rgba8unorm; rgb=normal, a=1)
// - Performs a 2x2 box filter in linear space and re-normalizes the result
//=========================================================


//=========================================================
// Bind Group Declarations
//=========================================================

@group(0) @binding(0) var previousMipLevel: texture_2d<f32>;
@group(0) @binding(1) var nextMipLevel: texture_storage_2d<rgba8unorm, write>;


//=========================================================
// Compute Shader Entry Point
// - Workgroup: 8x8 threads
// - Each invocation writes one texel in mip L at coordinate id.xy
//=========================================================

@compute @workgroup_size(8, 8)
fn computeMipMap(@builtin(global_invocation_id) id: vec3<u32>) {
    let o = vec2<u32>(0u, 1u);
    let base = 2u * id.xy;

    // Decode normals from [0,1] to [-1,1]
    let t0 = textureLoad(previousMipLevel, vec2<i32>(base + o.xx), 0).xyz * 2.0 - 1.0;
    let t1 = textureLoad(previousMipLevel, vec2<i32>(base + o.xy), 0).xyz * 2.0 - 1.0;
    let t2 = textureLoad(previousMipLevel, vec2<i32>(base + o.yx), 0).xyz * 2.0 - 1.0;
    let t3 = textureLoad(previousMipLevel, vec2<i32>(base + o.yy), 0).xyz * 2.0 - 1.0;

    // Average and renormalize
    var n = normalize(t0 + t1 + t2 + t3);

    // Re-encode to [0,1]
    let enc = n * 0.5 + 0.5;
    textureStore(nextMipLevel, id.xy, vec4<f32>(enc, 1.0));
}

