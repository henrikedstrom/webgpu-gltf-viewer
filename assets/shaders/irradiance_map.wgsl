@group(0) @binding(0) var environmentSampler: sampler;
@group(0) @binding(1) var environmentTexture: texture_cube<f32>;
@group(0) @binding(2) var irradianceMap: texture_storage_2d_array<rgba16float, write>; // Output irradiance cubemap
@group(1) @binding(0) var<uniform> faceIndex: u32; // Current face being computed

const PI: f32 = 3.14159265359;
const SAMPLE_COUNT: u32 = 1024;

fn generateTBN(normal: vec3<f32>) -> mat3x3<f32> {
    // Default 'bitangent' is world-space up (0,1,0)
    var bitangent = vec3<f32>(0.0, 1.0, 0.0);

    let NdotUp = dot(normal, vec3<f32>(0.0, 1.0, 0.0));
    let epsilon = 0.0000001;

    // If the normal is too close to +Y or -Y, pick a safer 'bitangent'
    if (1.0 - abs(NdotUp) <= epsilon) {
        if (NdotUp > 0.0) {
            bitangent = vec3<f32>(0.0, 0.0, 1.0);
        } else {
            bitangent = vec3<f32>(0.0, 0.0, -1.0);
        }
    }

    // Compute tangent by crossing bitangent and normal
    let tangent = normalize(cross(bitangent, normal));

    // Recompute bitangent to ensure orthogonality
    bitangent = cross(normal, tangent);

    // Return the TBN matrix
    return mat3x3<f32>(tangent, bitangent, normal);
}

// Van der Corput radical inverse for Hammersley sequence
fn RadicalInverse_VdC(bitsIn: u32) -> f32 {
    var bits = bitsIn;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return f32(bits) * 2.3283064365386963e-10; // Divide by 2^32
}

// Hammersley point generation for importance sampling
fn Hammersley(i: u32, N: u32) -> vec2<f32> {
    return vec2<f32>(f32(i) / f32(N), RadicalInverse_VdC(i));
}

fn ImportanceSampleHemisphere(sampleIndex: u32, sampleCount: u32, normal: vec3<f32>) -> vec3<f32> {
    // Compute pseudo-random spherical coordinates [0..1]
    let xi = Hammersley(sampleIndex, sampleCount);

    // Convert to spherical angles
    let phi = 2.0 * PI * xi.x;
    let cosTheta = sqrt(1.0 - xi.y);
    let sinTheta = sqrt(xi.y);

    // Local hemisphere direction:
    //    - z-axis points "up" (cosTheta).
    //    - x/y form the circle at sinTheta.
    let localDir = vec3<f32>(
        sinTheta * cos(phi),  // x
        sinTheta * sin(phi),  // y
        cosTheta              // z
    );

    // Generate a TBN matrix from the input normal
    let tbn = generateTBN(normal);

    // Transform local direction into world space and normalize
    let worldDir = normalize(tbn * localDir);
    return worldDir;
}


// Convert UV coordinates to a normalized direction vector
fn UVToDirection(uv: vec2<f32>, face: u32) -> vec3<f32> {

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

    // Map uv from [0,1] → [-1,1]
    let u = (uv.x * 2.0) - 1.0;
    let v = (uv.y * 2.0) - 1.0;

    let faceDir   = faceDirs[face];
    let up        = upVectors[face];
    let right     = rightVectors[face];

    let direction = normalize(faceDir + (u * right) + (v * up));

    return direction;
}

@compute @workgroup_size(8, 8)
fn computeIrradiance(@builtin(global_invocation_id) id: vec3<u32>) {
    let outputSize = textureDimensions(irradianceMap).xy;

    if (id.x >= outputSize.x || id.y >= outputSize.y) {
        return;
    }

    let uv = vec2<f32>(f32(id.x) / f32(outputSize.x), f32(id.y) / f32(outputSize.y));
    let normal = normalize(UVToDirection(uv, faceIndex));

    var irradiance = vec3<f32>(0.0);
    var weightSum = 0.0;

    for (var i = 0u; i < SAMPLE_COUNT; i++) {
        let sampleDir = ImportanceSampleHemisphere(i, SAMPLE_COUNT, normal);
        // TODO: Add lod bias to avoid noise from high-frequency environment maps
        let sampleColor = textureSampleLevel(environmentTexture, environmentSampler, sampleDir, 0).rgb;
        let weight = max(dot(normal, sampleDir), 0.0);

        irradiance += sampleColor * weight;
        weightSum += weight;
    }

    irradiance /= weightSum;

    textureStore(irradianceMap, id.xy, faceIndex, vec4<f32>(irradiance, 1.0));
}
