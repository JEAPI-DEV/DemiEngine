$input a_position, i_data0, i_data1, i_data2, i_data3, i_data4
$output v_color0, v_normal, v_texcoord0, v_worldPos, v_relativePos, v_terrainBlend, v_localPos, v_localNormal

#include "bgfx_shader.sh"

uniform vec4 u_viewPosition;

SAMPLER2D(s_terrainHeight, 3);
SAMPLER2D(s_terrainNormal, 4);
// Sample dimensions and local world units per sample in X/Z.
uniform vec4 u_terrainField;

void main()
{
    vec2 gridSample = a_position.xz + i_data4.xy;
    vec2 sampleUv = (gridSample + vec2(0.5)) / u_terrainField.xy;
    vec2 heights = texture2DLod(s_terrainHeight, sampleUv, 0.0).rg;
    float height = mix(heights.y, heights.x, i_data4.w);
    vec3 position = vec3(gridSample.x * u_terrainField.z, height,
                         gridSample.y * u_terrainField.w);
    vec3 normal = texture2DLod(s_terrainNormal, sampleUv, 0.0).xyz;
    mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
    vec4 worldPosition = mul(model, vec4(position, 1.0));
    gl_Position = mul(u_viewProj, worldPosition);
    vec3 modelX = model[0].xyz;
    vec3 modelY = model[1].xyz;
    vec3 modelZ = model[2].xyz;
    float handedness = dot(modelX, cross(modelY, modelZ)) < 0.0 ? -1.0 : 1.0;
    v_normal = handedness * (cross(modelY, modelZ) * normal.x +
                             cross(modelZ, modelX) * normal.y +
                             cross(modelX, modelY) * normal.z);
    v_localPos = position;
    v_localNormal = normal;
    v_worldPos = worldPosition.xyz;
    v_relativePos = v_worldPos - u_viewPosition.xyz;
    v_texcoord0 = sampleUv;
    v_color0 = vec4(1.0);
    v_terrainBlend = i_data4.z;
}
