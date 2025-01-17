
//---------------------------------------------------------------------
// Uniforms

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

//---------------------------------------------------------------------
// Constants and Types

const pi = 3.141592653589793;

struct MaterialInfo {
    baseColor: vec3f,
    metallic: f32,
    perceptualRoughness: f32,
    alphaRoughness: f32,
    f0: vec3f,
    f90: vec3f,
    cDiffuse: vec3f
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
    @location(4) viewDirectionWorld: vec3<f32>  // View direction (in World Space)
};

//---------------------------------------------------------------------
// Bind Groups

@group(0) @binding(0) var<uniform> globalUniforms: GlobalUniforms;
@group(0) @binding(1) var environmentSampler: sampler;
@group(0) @binding(2) var environmentTexture: texture_2d<f32>;
@group(0) @binding(3) var environmentIrradianceTexture: texture_2d<f32>;

@group(1) @binding(0) var<uniform> modelUniforms: ModelUniforms;
@group(1) @binding(1) var textureSampler: sampler;
@group(1) @binding(2) var baseColorTexture: texture_2d<f32>;
@group(1) @binding(3) var metallicRoughnessTexture: texture_2d<f32>;
@group(1) @binding(4) var normalTexture: texture_2d<f32>;
@group(1) @binding(5) var occlusionTexture: texture_2d<f32>;
@group(1) @binding(6) var emissiveTexture: texture_2d<f32>;


//---------------------------------------------------------------------
// Utility Functions

fn clampedDot(a: vec3f, b: vec3f) -> f32 {
  return clamp(dot(a, b), 0.0, 1.0);
}

fn getNormal(in: VertexOutput) -> vec3f {
  let tangent_normal = textureSample(normalTexture, textureSampler, in.texCoord0).xyz * 2.0 - 1.0;

  let q1 = dpdx(in.position.xyz);
  let q2 = dpdy(in.position.xyz);
  let st1 = dpdx(in.texCoord0);
  let st2 = dpdy(in.texCoord0);

  let N = normalize(in.normalWorld);
  let T = normalize(q1 * st2.y - q2 * st1.y);
  let B = -normalize(cross(N, T));
  let TBN = mat3x3f(T, B, N);

  return normalize(TBN * tangent_normal);
}

// https://google.github.io/filament/Filament.md.html#materialsystem/specularbrdf, Eq. 18
fn FSchlick(f0: vec3f, f90: vec3f, vDotH: f32) -> vec3f {
    return f0 + (f90 - f0) * pow(clamp(1.0 - vDotH, 0.0, 1.0), 5.0);
}

fn BRDFLambertian(f0: vec3f, f90: vec3f, diffuseColor: vec3f, specularWeight: f32, vDotH: f32) -> vec3f {
    return (1.0 - specularWeight * FSchlick(f0, f90, vDotH)) * (diffuseColor / pi);
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
  const gamma = 1.0;
  const invGamma = 1.0 / gamma;

  const exposure = 15.0;

  var color = colorIn * exposure;

  color = toneMapPBRNeutral(color);

  // Linear to sRGB
  color = pow(color, vec3f(invGamma));

  return color;
}

//---------------------------------------------------------------------
// Vertex Shader

@vertex
fn vertexMain(in: VertexInput) -> VertexOutput {
   
    // Transform the position to world space
    let worldPosition = modelUniforms.modelMatrix * vec4<f32>(in.position, 1.0);

    // Transform the normal to world space using the normal matrix (3x3 inverse transpose)
    let worldNormal = (modelUniforms.normalMatrix * vec4f(in.normal, 0.0)).xyz;

    var output: VertexOutput;
    output.position = globalUniforms.projectionMatrix * globalUniforms.viewMatrix * worldPosition;
    output.color = in.color;
    output.texCoord0 = in.texCoord0;
    output.texCoord1 = in.texCoord1;
    output.normalWorld = worldNormal;
    output.viewDirectionWorld = globalUniforms.cameraPositionWorld - worldPosition.xyz;
    return output;
}

//---------------------------------------------------------------------
// Fragment Shader

@fragment
fn fragmentMain(in: VertexOutput) -> @location(0) vec4f {

    // Set up material properties
    let metallicRoughness = textureSample(metallicRoughnessTexture, textureSampler, in.texCoord0).rgb;

    var materialInfo: MaterialInfo;
    materialInfo.baseColor = in.color.rgb * textureSample(baseColorTexture, textureSampler, in.texCoord0).rgb;
    materialInfo.metallic = metallicRoughness.b; // TODO: Multiply by metallic factor
    materialInfo.perceptualRoughness = metallicRoughness.g; // TODO: Multiply by roughness factor
    materialInfo.alphaRoughness = metallicRoughness.g * metallicRoughness.g;
    materialInfo.f0 = mix(vec3f(0.04), materialInfo.baseColor, materialInfo.metallic);
    materialInfo.f90 = vec3f(1.0);
    materialInfo.cDiffuse = mix(materialInfo.baseColor.rgb * 0.5, vec3f(0.0), materialInfo.metallic);
    
    let n = getNormal(in);
	let v = normalize(in.viewDirectionWorld);
	
    var diffuse = vec3f(0.0);
    var specular = vec3f(0.0);

    // Direct lighting
    {
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
            diffuse += lightColor * nDotL * BRDFLambertian(materialInfo.f0, materialInfo.f90, materialInfo.cDiffuse, 1.0, nDotH);
            specular += lightColor * nDotL * BRDFSpecularGGX(materialInfo.f0, materialInfo.f90, materialInfo.alphaRoughness, 1.0, vDotH, nDotL, nDotV, nDotH);
        }
    }

    // Environment lighting
    {
        // Convert the direction vector to spherical coordinates
        let theta = -acos(in.normalWorld.y); // Polar angle
        let phi = atan2(in.normalWorld.z, in.normalWorld.x); // Azimuthal angle

        // Convert to UV coordinates
        let iblUv = vec2f(phi / (2.0 * pi), 1.0 - theta / pi);

        // Sample the irradiance texture
        let irrad = textureSample(environmentIrradianceTexture, environmentSampler, iblUv).rgb;
        diffuse += irrad * BRDFLambertian(materialInfo.f0, materialInfo.f90, materialInfo.cDiffuse, 1.0, 1.0);
    }

    let ao = textureSample(occlusionTexture, textureSampler, in.texCoord0).rgb; // TODO: apply occlusion factor
    diffuse *= ao; 
    specular *= ao; 
    
    let emissive = textureSample(emissiveTexture, textureSampler, in.texCoord0).rgb;
    
    var color = emissive + diffuse + specular;

    color = toneMap(color);
    
    return vec4f(color, 1.0);
}
