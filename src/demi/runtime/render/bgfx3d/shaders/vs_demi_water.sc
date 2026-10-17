$input a_position, a_normal, a_texcoord1
$output v_waterNormal, v_waterLocal, v_waterRelative, v_waterData

#include "bgfx_shader.sh"
uniform vec4 u_waterCamera;

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    vec3 modelX = u_model[0][0].xyz;
    vec3 modelY = u_model[0][1].xyz;
    vec3 modelZ = u_model[0][2].xyz;
    float handedness = dot(modelX, cross(modelY, modelZ)) < 0.0 ? -1.0 : 1.0;
    v_waterNormal = handedness * (cross(modelY, modelZ) * a_normal.x +
        cross(modelZ, modelX) * a_normal.y + cross(modelX, modelY) * a_normal.z);
    v_waterLocal = a_position;
    v_waterRelative = mul(u_model[0], vec4(a_position, 1.0)).xyz - u_waterCamera.xyz;
    // Exact prepared local depth and local X/Z flow velocity, not baked RGBA.
    v_waterData = a_texcoord1;
}
