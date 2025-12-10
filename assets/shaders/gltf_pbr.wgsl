//=========================================================
// glTF PBR (metallic-roughness) shading
// - Vertex + fragment with IBL (irradiance, prefiltered specular, BRDF LUT)
// - Inputs: GlobalUniforms, ModelUniforms, MaterialUniforms, PBR textures
// - Output: tone-mapped sRGB color
//=========================================================

//=========================================================
// Uniforms & Bind Group Declarations
//=========================================================

struct GlobalUniforms {
    viewMatrix: mat4x4<f32>,
    projectionMatrix: mat4x4<f32>,
    inverseViewMatrix: mat4x4<f32>,
    inverseProjectionMatrix: mat4x4<f32>,
    cameraPositionWorld: vec3<f32>
};

struct ModelUniforms {
    modelMatrix: mat4x4<f32>,
    normalMatrix: mat4x4<f32>
};

struct MaterialUniforms {
    baseColorFactor: vec4<f32>,
    emissiveFactor: vec3<f32>,
    metallicFactor: f32,
    roughnessFactor: f32,
    normalScale: f32,
    occlusionStrength: f32,
    alphaCutoff: f32, 
    alphaMode: i32,   // 0 = Opaque, 1 = Mask, 2 = Blend
};

@group(0) @binding(0) var<uniform> globalUniforms: GlobalUniforms;
@group(0) @binding(1) var iblSampler: sampler;
@group(0) @binding(2) var environmentTexture: texture_cube<f32>;
@group(0) @binding(3) var iblIrradianceTexture: texture_cube<f32>;
@group(0) @binding(4) var iblSpecularTexture: texture_cube<f32>;
@group(0) @binding(5) var iblBRDFIntegrationLUTTexture: texture_2d<f32>;
@group(0) @binding(6) var iblBRDFIntegrationLUTSampler: sampler;

@group(1) @binding(0) var<uniform> modelUniforms: ModelUniforms;
@group(1) @binding(1) var<uniform> materialUniforms: MaterialUniforms;
@group(1) @binding(2) var textureSampler: sampler;
@group(1) @binding(3) var baseColorTexture: texture_2d<f32>;
@group(1) @binding(4) var metallicRoughnessTexture: texture_2d<f32>;
@group(1) @binding(5) var normalTexture: texture_2d<f32>;
@group(1) @binding(6) var occlusionTexture: texture_2d<f32>;
@group(1) @binding(7) var emissiveTexture: texture_2d<f32>;


//=========================================================
// Constants & Types
//=========================================================

const pi = 3.141592653589793;

struct MaterialInfo {
    baseColor: vec4f,
    metallic: f32,
    perceptualRoughness: f32,
    f0_dielectric: vec3f,
    alphaRoughness: f32,
    f0: vec3f,
    f90: vec3f,
    cDiffuse: vec3f,
    specularWeight: f32
};

struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) tangent: vec4<f32>,
    @location(3) texCoord0: vec2<f32>,
    @location(4) texCoord1: vec2<f32>,
    @location(5) color: vec4<f32>
};

struct VertexOutput {
    @builtin(position) position: vec4<f32>,     // Clip-space position
    @location(0) color: vec4<f32>,              // Vertex color
    @location(1) texCoord0: vec2<f32>,          // Texture coordinate 0
    @location(2) texCoord1: vec2<f32>,          // Texture coordinate 1
    @location(3) normalWorld: vec3<f32>,        // Normal vector (in World Space)
    @location(4) tangentWorld: vec4<f32>,       // Tangent vector (in World Space)
    @location(5) viewDirectionWorld: vec3<f32>  // View direction (in World Space)
};


//=========================================================
// Utility Functions
//=========================================================

fn clampedDot(a: vec3f, b: vec3f) -> f32 {
  return clamp(dot(a, b), 0.0, 1.0);
}

fn getNormal(in: VertexOutput) -> vec3f {
    // Reconstruct the TBN matrix using interpolated normal and tangent
    let N = normalize(in.normalWorld);
    let T = normalize(in.tangentWorld.xyz);
    let B = cross(N, T) * in.tangentWorld.w; // Tangent.w is handedness
    let TBN = mat3x3f(T, B, N);

    // Sample the normal map and remap from [0,1] to [-1,1]
    var sampledNormal = textureSample(normalTexture, textureSampler, in.texCoord0).xyz  * 2.0 - 1.0;
    sampledNormal *= materialUniforms.normalScale; 

    // Compute the final normal in world space
    return normalize(TBN * sampledNormal);
}

// https://google.github.io/filament/Filament.md.html#materialsystem/specularbrdf, Eq. 18
fn FSchlick(f0: vec3f, f90: vec3f, vDotH: f32) -> vec3f {
    return f0 + (f90 - f0) * pow(clamp(1.0 - vDotH, 0.0, 1.0), 5.0);
}

fn BRDFLambertian(f0: vec3f, f90: vec3f, diffuseColor: vec3f, specularWeight: f32, vDotH: f32) -> vec3f {
    return (1.0 - specularWeight * FSchlick(f0, f90, vDotH)) * (diffuseColor / pi);
}

// A helper function for sampling the environment map at a given LOD
fn samplePrefilteredSpecularIBL(reflection: vec3<f32>, lod: f32) -> vec4<f32> {
    let sampleColor = textureSampleLevel(iblSpecularTexture, iblSampler, reflection, lod);
    return sampleColor;
}

// Computes GGX prefiltered specular lighting from the environment
fn getIBLRadianceGGX(n: vec3<f32>, v: vec3<f32>, roughness: f32) -> vec3<f32> {
 
    // Compute the dot product of normal and view vector, clamped to [0,1]
    let NdotV = max(dot(n, v), 0.0);

    // Derive the LOD based on roughness and total mip count
    let lod = roughness * (f32(10) - 1.0);

    // Reflect the view vector around the normal
    let reflection = normalize(reflect(-v, n));

    // Sample the prefiltered environment
    let specularSample = samplePrefilteredSpecularIBL(reflection, lod);

    // Return the RGB channels as the specular lighting
    return specularSample.rgb;
}

// Computes the environment Fresnel reflectance using a GGX BRDF LUT, accounting for single and multiple scattering.
fn getIBLGGXFresnel(n: vec3<f32>, v: vec3<f32>, roughness: f32, F0: vec3<f32>, specularWeight: f32) -> vec3<f32> 
{
    // Compute the dot product of normal and view vector, clamped to [0,1]
    let NdotV = max(dot(n, v), 0.0);

    // Lookup coordinates for the BRDF integration LUT (NdotV, roughness)
    let brdfLUTCoords = vec2<f32>(NdotV, roughness);

    // Sample the precomputed GGX LUT (stores scale and bias for Fresnel-Schlick approximation)
    let brdfLUTSample = textureSample(iblBRDFIntegrationLUTTexture, iblBRDFIntegrationLUTSampler, brdfLUTCoords);
    let brdfLUT = brdfLUTSample.rg; // .x = scale factor, .y = bias term

    // Single-scattering Fresnel component (Fdez-Aguera approximation)
    // "fresnelPivot" adjusts F0 based on roughness to account for microfacet distribution
    let fresnelPivot = max(vec3<f32>(1.0 - roughness), F0) - F0;
    let fresnelSingleScatter = F0 + fresnelPivot * pow(1.0 - NdotV, 5.0);

    // Compute the weighted single-scattering specular term
    let FssEss = specularWeight * (fresnelSingleScatter * brdfLUT.x + brdfLUT.y);

    // Multiple-scattering Fresnel component
    let Ems = 1.0 - (brdfLUT.x + brdfLUT.y); // Energy conservation term
    let F_avg = specularWeight * (F0 + (1.0 - F0) / 21.0); // Approximated average Fresnel reflectance
    let FmsEms = Ems * FssEss * F_avg / (1.0 - F_avg * Ems);

    // Final Fresnel reflection including multiple scattering
    return FssEss + FmsEms;
}

// https://google.github.io/filament/Filament.md.html#materialsystem/specularbrdf/geometricshadowing(specularg), Listing 3
fn VGGX(nDotL: f32, nDotV: f32, alphaRoughness: f32) -> f32 {
    let a2 = alphaRoughness * alphaRoughness;

    let ggxV = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
    let ggxL = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);

    let ggx = ggxV + ggxL;
    if (ggx > 0.0) {
        return 0.5 / ggx;
    }
    return 0.0;
}

// https://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html, Eq. 4
fn DGGX(nDotH: f32, alphaRoughness: f32) -> f32 {
    let alphaRoughnessSq = alphaRoughness * alphaRoughness;
    let f = (nDotH * nDotH) * (alphaRoughnessSq - 1.0) + 1.0;
    return alphaRoughnessSq / (pi * f * f);
}

fn BRDFSpecularGGX(f0: vec3f, f90: vec3f, alphaRoughness: f32, specularWeight: f32, vDotH: f32, nDotL: f32, nDotV: f32, nDotH: f32) -> vec3f {
    let F = FSchlick(f0, f90, vDotH);
    let V = VGGX(nDotL, nDotV, alphaRoughness);
    let D = DGGX(nDotH, alphaRoughness);

    return specularWeight * F * V * D;
}

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


//=========================================================
// Vertex Shader
//=========================================================

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {

    // Transform position and normal to world space
    let worldPosition = modelUniforms.modelMatrix * vec4<f32>(in.position, 1.0);
    let worldNormal = normalize((modelUniforms.normalMatrix * vec4<f32>(in.normal, 0.0)).xyz);

    // Transform tangent to world space (preserving handedness in .w)
    let worldTangent = vec4<f32>(
        normalize((modelUniforms.normalMatrix * vec4<f32>(in.tangent.xyz, 0.0)).xyz),
        in.tangent.w
    );

    var output: VertexOutput;
    output.position = globalUniforms.projectionMatrix * globalUniforms.viewMatrix * worldPosition;
    output.color = in.color;
    output.texCoord0 = in.texCoord0;
    output.texCoord1 = in.texCoord1;
    output.normalWorld = worldNormal;
    output.tangentWorld = worldTangent;
    output.viewDirectionWorld = globalUniforms.cameraPositionWorld - worldPosition.xyz;
    return output;
}


//=========================================================
// Fragment Shader
//=========================================================

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {

    // Sample base color and metallic-roughness textures
    let baseColor = textureSample(baseColorTexture, textureSampler, in.texCoord0).rgba;
    let metallicRoughness = textureSample(metallicRoughnessTexture, textureSampler, in.texCoord0).rgb;

    // Fill out the material info struct
    var materialInfo: MaterialInfo;
    materialInfo.baseColor = baseColor * in.color * materialUniforms.baseColorFactor;
    materialInfo.metallic = metallicRoughness.b * materialUniforms.metallicFactor;
    materialInfo.perceptualRoughness = metallicRoughness.g * materialUniforms.roughnessFactor;
    materialInfo.f0_dielectric = vec3f(0.04);
    materialInfo.specularWeight = 1.0;
    materialInfo.alphaRoughness = metallicRoughness.g * metallicRoughness.g;
    materialInfo.f0 = mix(vec3f(0.04), materialInfo.baseColor.rgb, materialInfo.metallic);
    materialInfo.f90 = vec3f(1.0);
    materialInfo.cDiffuse = mix(materialInfo.baseColor.rgb * 0.5, vec3f(0.0), materialInfo.metallic);
    
    let n = getNormal(in);
	let v = normalize(in.viewDirectionWorld);
	
    var color = vec3f(0.0);

    // Environment lighting
    {
        // Sample the irradiance texture
        let diffuseEnv = textureSample(iblIrradianceTexture, iblSampler, in.normalWorld).rgb;
        let iblDiffuse = diffuseEnv * materialInfo.baseColor.rgb;

        // Sample the specular texture
        let iblSpecular         = getIBLRadianceGGX(n, v, materialInfo.perceptualRoughness);
        let fresnelDielectric   = getIBLGGXFresnel(n, v, materialInfo.perceptualRoughness, materialInfo.f0_dielectric, materialInfo.specularWeight);
        let iblDielectric       = mix(iblDiffuse, iblSpecular, fresnelDielectric);
        let fresnelMetal        = getIBLGGXFresnel(n, v, materialInfo.perceptualRoughness, materialInfo.baseColor.rgb, 1.0);
        let iblMetal            = fresnelMetal * iblSpecular;

        color += mix(iblDielectric, iblMetal, materialInfo.metallic);
    }

    let ao = textureSample(occlusionTexture, textureSampler, in.texCoord0).r * materialUniforms.occlusionStrength;
    color *= vec3f(ao);

    // Direct lighting
    if (false) {
        // Define a global light
        let globalLightDirWorld: vec3<f32> = normalize(vec3<f32>(1.0, 1.0, 1.0));
        let lightColor: vec3<f32> = vec3<f32>(1.0, 0.9, 0.7); // Slight yellow tint

        // Calculate the lighting terms
        let l = normalize(globalLightDirWorld);
        let h = normalize(l + v);

        let nDotL = clampedDot(n, l);
        let nDotV = clampedDot(n, v);
        let nDotH = clampedDot(n, h);
        let lDotH = clampedDot(l, h);
        let vDotH = clampedDot(v, h);

        if (nDotL >= 0.0 && nDotV >= 0.0) {
            color += lightColor * nDotL * BRDFLambertian(materialInfo.f0, materialInfo.f90, materialInfo.cDiffuse, 1.0, nDotH);
            color += lightColor * nDotL * BRDFSpecularGGX(materialInfo.f0, materialInfo.f90, materialInfo.alphaRoughness, 1.0, vDotH, nDotL, nDotV, nDotH);
        }
    }

    // Emissive   
    var emissive = textureSample(emissiveTexture, textureSampler, in.texCoord0).rgb;
    emissive *= materialUniforms.emissiveFactor;
    color += emissive;

    // Alpha masking. This needs to happen after all texture lookups to ensure correct gradient/derivatives.
    if (materialUniforms.alphaMode == 1) { // Mask mode
        if (baseColor.a < materialUniforms.alphaCutoff) {
            discard;
        }
    }

    // Final color output
    color = toneMap(color);

    // Select alpha: Opaque forces 1.0, Mask/Blend use factored alpha (baseColor.a)
    var alpha = select(materialInfo.baseColor.a, 1.0, materialUniforms.alphaMode == 0); 
    return vec4f(color, alpha);
}
