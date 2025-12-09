//=========================================================
// 2D sRGB mip downsample (render path)
// - prevTexture: sRGB view of mip L-1 (HW sRGB decode on sample)
// - target: sRGB mip L (HW encode on output), viewport = mip L size
// - Fullscreen triangle; manual 2x2 PMA box filter for correct alpha
//=========================================================


//=========================================================
// Bind Group Declarations
//=========================================================

@group(0) @binding(0) var prevTexture: texture_2d<f32>;


//=========================================================
// Constants & Types
//=========================================================

const kAlphaEpsilon: f32 = 1e-6; // Alpha threshold for avoiding division by zero

struct VertexOutput {
  @builtin(position) pos: vec4<f32>,
  @location(0) uv: vec2<f32>,
};


//=========================================================
// Vertex Shader
//=========================================================

@vertex
fn vs_main(@builtin(vertex_index) vid: u32) -> VertexOutput {
  // Fullscreen triangle
  var positions = array<vec2<f32>, 3>(
    vec2<f32>(-1.0, -3.0),
    vec2<f32>(3.0, 1.0),
    vec2<f32>(-1.0, 1.0)
  );
  var uv = array<vec2<f32>, 3>(
    vec2<f32>(0.0, 2.0),
    vec2<f32>(2.0, 0.0),
    vec2<f32>(0.0, 0.0)
  );
  var out: VertexOutput;
  out.pos = vec4<f32>(positions[vid], 0.0, 1.0);
  out.uv = uv[vid];
  return out;
}


//=========================================================
// Fragment Shader
//=========================================================

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
  // Manual 2x2 PMA box filter to handle straight alpha inputs (glTF typical).
  // Use textureLoad to get exact texel values without sampler filtering.
  let prevSize = vec2<i32>(textureDimensions(prevTexture, 0));
  
  // Convert fragment position to integer pixel coordinates in output mip
  // The viewport is set to the output mip size, so position.xy is the pixel coordinate
  let outputCoord = vec2<i32>(i32(in.pos.x), i32(in.pos.y));
  
  // Calculate the 2x2 block coordinates in the previous mip level
  let baseCoord = 2i * outputCoord;
  
  // Sample the 4 texels using textureLoad (exact values, no filtering)
  // textureLoad on sRGB texture view returns linear values (GPU auto-decodes sRGB)
  let s0 = textureLoad(prevTexture, baseCoord + vec2<i32>(0, 0), 0);
  let s1 = textureLoad(prevTexture, baseCoord + vec2<i32>(1, 0), 0);
  let s2 = textureLoad(prevTexture, baseCoord + vec2<i32>(0, 1), 0);
  let s3 = textureLoad(prevTexture, baseCoord + vec2<i32>(1, 1), 0);

  // Premultiplied alpha box filter in linear space
  let alphaSum = s0.a + s1.a + s2.a + s3.a;
  let alpha    = 0.25 * alphaSum;

  let colorPremul = s0.rgb * s0.a + s1.rgb * s1.a + s2.rgb * s2.a + s3.rgb * s3.a;
  let invAlpha = 1.0 / max(alphaSum, kAlphaEpsilon);
  let rgb = select(colorPremul * invAlpha, vec3<f32>(0.0), alphaSum <= kAlphaEpsilon);

  // Return in linear space - the render target's sRGB format will encode it back to sRGB
  return vec4<f32>(rgb, alpha);
}

