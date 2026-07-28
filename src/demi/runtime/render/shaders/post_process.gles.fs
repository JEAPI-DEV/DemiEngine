#version 100
precision mediump float;
varying vec2 fragTexCoord;
varying vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 resolution;
uniform float exposure;
uniform float contrast;
uniform float saturation;
uniform float vignette;
uniform float bloom;
uniform float bloomThreshold;
uniform vec4 tint;
void main() {
  vec2 texel = 1.0/max(resolution, vec2(1.0));
  vec3 color = texture2D(texture0, fragTexCoord).rgb;
  vec3 glow = max(color-vec3(bloomThreshold), vec3(0.0));
  glow += max(texture2D(texture0, fragTexCoord+vec2(texel.x, 0.0)).rgb-vec3(bloomThreshold), vec3(0.0));
  glow += max(texture2D(texture0, fragTexCoord-vec2(texel.x, 0.0)).rgb-vec3(bloomThreshold), vec3(0.0));
  glow += max(texture2D(texture0, fragTexCoord+vec2(0.0, texel.y)).rgb-vec3(bloomThreshold), vec3(0.0));
  glow += max(texture2D(texture0, fragTexCoord-vec2(0.0, texel.y)).rgb-vec3(bloomThreshold), vec3(0.0));
  color += glow*(bloom*0.2);
  color *= exp2(exposure);
  color = (color-0.5)*contrast+0.5;
  float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
  color = mix(vec3(luminance), color, saturation)*tint.rgb;
  vec2 centered = fragTexCoord*2.0-1.0;
  float edge = smoothstep(0.35, 1.25, dot(centered, centered));
  color *= 1.0-edge*vignette;
  gl_FragColor = vec4(color, 1.0)*fragColor;
}
