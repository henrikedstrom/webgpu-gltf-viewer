//=========================================================
// Global Uniforms & Bind Group Declarations
//=========================================================

// Bind Group 0 - Common parameters
@group(0) @binding(0) var inputSampler: sampler;
@group(0) @binding(1) var inputTexture: texture_2d<f32>;
@group(0) @binding(2) var outputTexture: texture_storage_2d_array<rgba16float, write>;

// Bind Group 1 - Per-face parameters
@group(1) @binding(0) var<uniform> faceIndex: u32;

//=========================================================
// Global Constants
//=========================================================

const PI: f32 = 3.14159265359;

//=========================================================
// Utility Functions
//=========================================================

// Converts a direction vector to equirectangular UV coordinates.
// Assumes a normalized direction; returns UV in [0,1].
fn dirToUV(dir: vec3<f32>) -> vec2<f32> {
    return vec2<f32>(
        0.5 + 0.5 * atan2(dir.z, dir.x) / PI,
        acos(clamp(dir.y, -1.0, 1.0)) / PI
    );
}

// Converts 2D UV coordinates (in [0,1], remapped internally to [-1,1])
// to a 3D direction based on the selected cube face.
fn uvToDirection(uv: vec2<f32>, face: u32) -> vec3<f32> {

    const faceDirs = array<vec3<f32>, 6>(
        vec3<f32>( 1.0,  0.0,  0.0), // +X
        vec3<f32>(-1.0,  0.0,  0.0), // -X
        vec3<f32>( 0.0,  1.0,  0.0), // +Y
        vec3<f32>( 0.0, -1.0,  0.0), // -Y
        vec3<f32>( 0.0,  0.0,  1.0), // +Z
        vec3<f32>( 0.0,  0.0, -1.0)  // -Z
    );

    const upVectors = array<vec3<f32>, 6>(
        vec3<f32>( 0.0, -1.0,  0.0), // +X
        vec3<f32>( 0.0, -1.0,  0.0), // -X
        vec3<f32>( 0.0,  0.0,  1.0), // +Y
        vec3<f32>( 0.0,  0.0, -1.0), // -Y
        vec3<f32>( 0.0, -1.0,  0.0), // +Z
        vec3<f32>( 0.0, -1.0,  0.0)  // -Z
    );

    const rightVectors = array<vec3<f32>, 6>(
        vec3<f32>( 0.0,  0.0, -1.0), // +X
        vec3<f32>( 0.0,  0.0,  1.0), // -X
        vec3<f32>( 1.0,  0.0,  0.0), // +Y
        vec3<f32>( 1.0,  0.0,  0.0), // -Y
        vec3<f32>( 1.0,  0.0,  0.0), // +Z
        vec3<f32>(-1.0,  0.0,  0.0)  // -Z
    );

    let u = (uv.x * 2.0) - 1.0;
    let v = (uv.y * 2.0) - 1.0;

    return normalize(faceDirs[face] + (u * rightVectors[face]) + (v * upVectors[face]));
}

//=========================================================
// Compute Shader Entry Point
//=========================================================

@compute @workgroup_size(8, 8)
fn panoramaToCubemap(@builtin(global_invocation_id) id: vec3<u32>) {

    // Get the dimensions of the output texture (assumed to be square)
    let outputSize = textureDimensions(outputTexture).xy;
    if (id.x >= outputSize.x || id.y >= outputSize.y) { 
        return; 
    }

    // Convert pixel coordinates (id.xy) to normalized [0,1] UV coordinates.
    let uvDst = vec2<f32>(f32(id.x) / f32(outputSize.x), f32(id.y) / f32(outputSize.y));
    let dir = uvToDirection(uvDst, faceIndex);

    // Convert the direction to equirectangular UV coordinates.
    let uvSrc = clamp(dirToUV(dir), vec2<f32>(0.0), vec2<f32>(1.0));

    // --- Manual Bilinear Filtering ---
    
    // Obtain the dimensions of the input (panorama) texture.
    let dims = vec2<i32>(textureDimensions(inputTexture));
    let width = f32(dims.x);
    let height = f32(dims.y);

    // Compute the source texel coordinates.
    let srcXF = uvSrc.x * (width - 1.0);
    let srcYF = uvSrc.y * (height - 1.0);

    // Compute the integer coordinate of the top-left texel.
    let x0 = i32(floor(srcXF));
    let y0 = i32(floor(srcYF));

    // Compute the neighboring texel positions:
    // - Wrap horizontally (equirectangular texture).
    // - Clamp vertically.
    let x1 = (x0 + 1) % dims.x;
    let y1 = min(y0 + 1, dims.y - 1);

    // Compute the fractional part for interpolation.
    let fx = srcXF - floor(srcXF);
    let fy = srcYF - floor(srcYF);

    // Fetch the four nearest texels.
    let c00 = textureLoad(inputTexture, vec2<i32>(x0, y0), 0);
    let c10 = textureLoad(inputTexture, vec2<i32>(x1, y0), 0);
    let c01 = textureLoad(inputTexture, vec2<i32>(x0, y1), 0);
    let c11 = textureLoad(inputTexture, vec2<i32>(x1, y1), 0);

    // Interpolate horizontally, then vertically.
    let top = mix(c00, c10, fx);
    let bottom = mix(c01, c11, fx);
    let color = mix(top, bottom, fy);

    // --- End Manual Filtering ---

    // Write the color to the output cubemap face.
    textureStore(outputTexture, id.xy, faceIndex, color);
}
