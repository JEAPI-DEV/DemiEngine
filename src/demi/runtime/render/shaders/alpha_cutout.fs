#version 330
in vec2 fragTexCoord;
in vec3 fragPosition;

uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec4 fogColor;
uniform float fogStart;
uniform float fogEnd;

out vec4 finalColor;

void main()
{
  vec4 texelColor = texture(texture0, fragTexCoord);
  if (texelColor.a < 0.5) {
    discard;
  }
  vec4 baseColor = texelColor*colDiffuse;
  float fogDistance = distance(viewPos, fragPosition);
  float fogAmount = clamp((fogEnd - fogDistance)/(fogEnd - fogStart), 0.0, 1.0);
  finalColor = mix(fogColor, baseColor, fogAmount);
}
