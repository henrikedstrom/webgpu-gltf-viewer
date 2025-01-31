@group(0) @binding(0) var previousMipLevel: texture_2d_array<f32>;
@group(0) @binding(1) var nextMipLevel: texture_storage_2d_array<rgba16float, write>;
@group(1) @binding(0) var<uniform> faceIndex: u32;

@compute @workgroup_size(8, 8)
fn computeMipMap(@builtin(global_invocation_id) id: vec3<u32>) {
    let offset = vec2<u32>(0u, 1u);

    let baseCoord = 2u * id.xy;
    let color = (
        textureLoad(previousMipLevel, vec2<i32>(baseCoord + offset.xx), i32(faceIndex), 0) +
        textureLoad(previousMipLevel, vec2<i32>(baseCoord + offset.xy), i32(faceIndex), 0) +
        textureLoad(previousMipLevel, vec2<i32>(baseCoord + offset.yx), i32(faceIndex), 0) +
        textureLoad(previousMipLevel, vec2<i32>(baseCoord + offset.yy), i32(faceIndex), 0)
    ) * 0.25;

    textureStore(nextMipLevel, id.xy, faceIndex, color);
}