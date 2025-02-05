//=========================================================
// Global Uniforms & Bind Group Declarations
//=========================================================

struct PrefilterParams {
    roughness: f32,
    numSamples: u32
};

// Bind Group 0 - Input: Environment texture
@group(0) @binding(0) var environmentSampler: sampler;
@group(0) @binding(1) var environmentTexture: texture_cube<f32>;

// Bind Group 1 - Input: Per-face parameters
@group(1) @binding(0) var<uniform> faceIndex: u32;

// Bind Group 2 - Input: Per-Mip parameters
@group(2) @binding(0) var<uniform> prefilterParams: PrefilterParams;

// Bind Group 3 - Output: Resulting texture
@group(3) @binding(0) var outputTexture: texture_storage_2d_array<rgba16float, write>;


//=========================================================
// Global Constants
//=========================================================

const PI: f32 = 3.14159265359;


//=========================================================
// Utility Functions
//=========================================================

fn saturate(x: f32) -> f32 {
    return clamp(x, 0.0, 1.0);
}

fn radicalInverseVdC(bitsIn: u32) -> f32 {
    var bits = bitsIn;
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return f32(bits) * 2.3283064365386963e-10;
}

fn hammersley2D(i: u32, N: u32) -> vec2<f32> {
    return vec2<f32>(f32(i) / f32(N), radicalInverseVdC(i));
}


//=========================================================
// Coordinate System Helpers
//=========================================================

fn generateTBN(normal: vec3<f32>) -> mat3x3<f32> {
    var bitangent = vec3<f32>(0.0, 1.0, 0.0);
    let NdotUp = dot(normal, bitangent);
    let epsilon = 1e-7;

    if (1.0 - abs(NdotUp) <= epsilon) {

        // If the normal is nearly parallel to world up, 
        // swap to Z to avoid a cross-product degeneracy.
        if (NdotUp > 0.0) {
            bitangent = vec3<f32>(0.0, 0.0, 1.0);
        } else {
            bitangent = vec3<f32>(0.0, 0.0, -1.0);
        }
    }

    let tangent = normalize(cross(bitangent, normal));
    bitangent = cross(normal, tangent);

    return mat3x3<f32>(tangent, bitangent, normal);
}

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
// Sampling Functions
//=========================================================

/// Generates a cosine-weighted (Lambertian) sample direction around the given
/// normal. Returns a vec4<f32> whose .xyz is the sampled direction, and .w is
/// the PDF (probability density function) for that direction.
///
/// For more details, see:
///   - https://www.pbr-book.org/3ed-2018/Monte_Carlo_Integration/2D_Sampling_with_Multidimensional_Transformations#Cosine-WeightedHemisphereSampling
///   - GPU Gems 3, Ch. 20 on Importance Sampling
fn importanceSampleLambertian(sampleIndex: u32, sampleCount: u32, normal: vec3<f32>) -> vec4<f32> {

    // Quasi-random point in the [0,1]^2 domain
    let xi = hammersley2D(sampleIndex, sampleCount);
    
    // Convert to spherical coordinates (cosine-weighted)
    let phi = 2.0 * PI * xi.x;
    let cosTheta = sqrt(1.0 - xi.y);
    let sinTheta = sqrt(xi.y);

    // Local hemisphere direction (z-axis = cosTheta)
    let localDir = vec3<f32>(
        sinTheta * cos(phi),
        sinTheta * sin(phi),
        cosTheta
    );
    
    // Transform local direction to world space using the normal's TBN
    let worldDir = normalize(generateTBN(normal) * localDir);

    // PDF for lambertian = cosTheta / PI in local space
    let pdf = cosTheta / PI;

    // Return the direction and the PDF
    return vec4<f32>(worldDir, pdf);
}

/// Evaluates the GGX (Trowbridge-Reitz) microfacet distribution function
/// at the given NdotH and alpha (roughness^2).
fn dGgx(NdotH: f32, alpha: f32) -> f32 {
    let a = NdotH * alpha;
    let k = alpha / (1.0 - NdotH*NdotH + a*a);
    return (k * k) / PI;
}

// GGX microfacet distribution
// https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.html
// This implementation is based on https://bruop.github.io/ibl/,
// https://www.tobias-franke.eu/log/2014/03/30/notes_on_importance_sampling.html
// and https://developer.nvidia.com/gpugems/GPUGems3/gpugems3_ch20.html
fn importanceSampleGgx(sampleIndex: u32, sampleCount: u32, normal: vec3<f32>, roughness: f32) -> vec4<f32> {

    // Quasi-random point in the [0,1]^2 domain
    let xi = hammersley2D(sampleIndex, sampleCount);

    // Compute alpha = (roughness)^2 and map xi to the GGX half-vector distribution.
    let alpha = roughness * roughness;
    let cosTheta = saturate(sqrt((1.0 - xi.y) / (1.0 + ((alpha * alpha) - 1.0) * xi.y)));
    let sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    let phi = 2.0 * PI * xi.x;

    // Local half-vector in tangent space
    let localHalf = vec3<f32>(
        sinTheta * cos(phi),
        sinTheta * sin(phi),
        cosTheta
    );

    // Convert localHalf to world space using TBN.
    let tbn = generateTBN(normal);
    let halfVec = normalize(tbn * localHalf);

    // Reflect the view vector (assumed to be 'normal') about the half-vector.
    let viewDir = normal;
    let reflectDir = normalize(reflect(-viewDir, halfVec));

    // Compute the PDF for the GGX distribution.
    let pdf = dGgx(cosTheta, alpha) / 4.0;

    // Return the reflected direction in .xyz and the PDF in .w.
    return vec4<f32>(reflectDir, pdf);
}

// Mipmap Filtered Samples (GPU Gems 3, 20.4)
// https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-20-gpu-based-importance-sampling
// https://cgg.mff.cuni.cz/~jaroslav/papers/2007-sketch-fis/Final_sap_0073.pdf
fn computeLod(pdf: f32, faceSize: f32) -> f32 {
    let numSamples: f32 = f32(prefilterParams.numSamples);

    // https://cgg.mff.cuni.cz/~jaroslav/papers/2007-sketch-fis/Final_sap_0073.pdf
    return 0.5 * log2((6.0 * faceSize * faceSize) / (numSamples * pdf));
}


//=========================================================
// Compute Shader Entry Points
//=========================================================

/// Generates a diffuse irradiance cube map face by sampling the environment map
/// (environmentTexture) using a Lambertian distribution. 
///
/// References:
///   - GPU Gems 3, Ch. 20: GPU-Based Importance Sampling
@compute @workgroup_size(8, 8)
fn computeIrradiance(@builtin(global_invocation_id) id: vec3<u32>) {

    let outputSize = textureDimensions(outputTexture).xy;
    if (id.x >= outputSize.x || id.y >= outputSize.y) { 
        return; 
    }

    // Convert (x, y) into [0,1] UV coordinates, then to a direction vector on the cube face.
    let uv = vec2<f32>(f32(id.x) / f32(outputSize.x), f32(id.y) / f32(outputSize.y));
    let normal = normalize(uvToDirection(uv, faceIndex));

    var irradiance = vec3<f32>(0.0);
    var weightSum = 0.0;
    
    // Sample directions around 'normal' using a cosine-weighted distribution (Lambertian).
    for (var i = 0u; i < prefilterParams.numSamples; i++) {

        let sample = importanceSampleLambertian(i, prefilterParams.numSamples, normal);
        let sampleDir = sample.xyz;
        let pdf = sample.w;
        
        // Compute the mip level based on the PDF and the output texture size (avoid high-frequency noise).
        let lod: f32 = computeLod(pdf, f32(outputSize.x));
        
        // Fetch environment map color at this direction + LOD.
        let sampleColor = textureSampleLevel(environmentTexture, environmentSampler, sampleDir, lod).rgb;

        // Weight each sample by dot(N, L) and accumulate.
        let weight = max(dot(normal, sampleDir), 0.0);
        irradiance += sampleColor * weight;
        weightSum += weight;
    }

    // Normalize the accumulation by the total weight sum to get final irradiance.
    if (weightSum > 0.0) {
        irradiance /= weightSum;
    }

    // Store the result in the output cubemap, at the appropriate face.
    textureStore(outputTexture, id.xy, faceIndex, vec4<f32>(irradiance, 1.0));
}

/// Generates a prefiltered (specular) environment map for the given face of a
/// cube map, using GGX (Trowbridge-Reitz) importance sampling.
@compute @workgroup_size(8, 8)
fn computePrefilteredSpecular(@builtin(global_invocation_id) id: vec3<u32>) {

    let outputSize = textureDimensions(outputTexture).xy;
    if (id.x >= outputSize.x || id.y >= outputSize.y) { 
        return; 
    }

    // Convert (x, y) into [0,1] UV coordinates, then to a direction vector on the cube face.
    let uv = vec2<f32>(f32(id.x) / f32(outputSize.x), f32(id.y) / f32(outputSize.y));
    let normal = normalize(uvToDirection(uv, faceIndex));

    let roughness = prefilterParams.roughness;
    var accumSpecular = vec3<f32>(0.0);
    var weightSum = 0.0;
    
    // GGX importance sampling around 'normal'
    for (var i = 0u; i < prefilterParams.numSamples; i++) {

        let sample = importanceSampleGgx(i, prefilterParams.numSamples, normal, roughness);
        let sampleDir = sample.xyz;
        let pdf = sample.w;
        
        // Convert the PDF to a suitable mip level in the environment map.
        let lod: f32 = computeLod(pdf, f32(outputSize.x));

        // Fetch environment map color at this direction + LOD.
        let sampleColor = textureSampleLevel(environmentTexture, environmentSampler, sampleDir, lod).rgb;
        
        // Weight each sample by dot(N, L) and accumulate.
        let weight = max(dot(normal, sampleDir), 0.0);
        accumSpecular += sampleColor * weight;
        weightSum += weight;
    }

    // Normalize by total weight
    if (weightSum > 0.0) {
        accumSpecular /= weightSum;
    }

    // Store the result in the output cubemap, at the appropriate face.
    textureStore(outputTexture, id.xy, faceIndex, vec4<f32>(accumSpecular, 1.0));
}
