#version 100
precision mediump float;
varying vec2 fragTexCoord;
varying vec3 fragPosition;
varying vec3 fragNormal;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec4 fogColor;
uniform float fogStart;
uniform float fogEnd;
uniform vec4 ambient;
uniform int lightCount;
struct RenderLight {
  vec3 position;
  vec3 direction;
  vec4 color;
  float range;
  float innerCos;
  float outerCos;
  int type;
};
uniform RenderLight lights[4];
void main() {
  vec4 texelColor = texture2D(texture0, fragTexCoord);
  if (texelColor.a < 0.5) discard;
  vec4 baseColor = texelColor*colDiffuse;
  vec3 normal = normalize(fragNormal);
  vec3 lighting = ambient.rgb*ambient.a;
  for (int index = 0; index < 4; ++index) {
    if (index >= lightCount) break;
    vec3 lightDirection = normalize(-lights[index].direction);
    float attenuation = 1.0;
    if (lights[index].type != 0) {
      vec3 offset = lights[index].position-fragPosition;
      float distanceToLight = length(offset);
      lightDirection = normalize(offset);
      attenuation = clamp(1.0-distanceToLight/max(lights[index].range, 0.001), 0.0, 1.0);
      attenuation *= attenuation;
      if (lights[index].type == 2) {
        float cone = dot(normalize(lights[index].direction), -lightDirection);
        attenuation *= smoothstep(lights[index].outerCos, lights[index].innerCos, cone);
      }
    }
    lighting += lights[index].color.rgb*lights[index].color.a*
                max(dot(normal, lightDirection), 0.0)*attenuation;
  }
  baseColor.rgb *= max(lighting, vec3(0.02));
  float fogDistance = distance(viewPos, fragPosition);
  float fogAmount = clamp((fogEnd-fogDistance)/max(fogEnd-fogStart, 0.001), 0.0, 1.0);
  gl_FragColor = mix(fogColor, baseColor, fogAmount);
}
