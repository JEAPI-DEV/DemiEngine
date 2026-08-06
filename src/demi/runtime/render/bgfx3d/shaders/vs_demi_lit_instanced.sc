$input a_position, a_normal, a_color0, a_texcoord0, i_data0, i_data1, i_data2, i_data3
$output v_color0, v_normal, v_texcoord0, v_worldPos

#include "bgfx_shader.sh"

void main()
{
    mat4 model = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
    vec4 worldPosition = mul(model, vec4(a_position, 1.0));
    gl_Position = mul(u_viewProj, worldPosition);
    vec3 modelX = model[0].xyz;
    vec3 modelY = model[1].xyz;
    vec3 modelZ = model[2].xyz;
    float handedness = dot(modelX, cross(modelY, modelZ)) < 0.0 ? -1.0 : 1.0;
    v_normal = handedness * (cross(modelY, modelZ) * a_normal.x +
                             cross(modelZ, modelX) * a_normal.y +
                             cross(modelX, modelY) * a_normal.z);
    v_worldPos = worldPosition.xyz;
    v_color0 = a_color0;
    v_texcoord0 = a_texcoord0;
}
