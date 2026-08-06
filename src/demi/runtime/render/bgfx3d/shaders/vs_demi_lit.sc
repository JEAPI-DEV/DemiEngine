$input a_position, a_normal, a_color0, a_texcoord0
$output v_color0, v_normal, v_texcoord0, v_worldPos

#include "bgfx_shader.sh"

void main()
{
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    // Cofactors are the inverse-transpose normal matrix multiplied by its
    // determinant. Normalization in the fragment stage removes that scalar.
    // Unlike multiplying by the model matrix, this remains correct when a
    // MeshRenderer uses non-uniform size or inherits non-uniform scale.
    vec3 modelX = u_model[0][0].xyz;
    vec3 modelY = u_model[0][1].xyz;
    vec3 modelZ = u_model[0][2].xyz;
    float handedness = dot(modelX, cross(modelY, modelZ)) < 0.0 ? -1.0 : 1.0;
    v_normal = handedness * (cross(modelY, modelZ) * a_normal.x +
                             cross(modelZ, modelX) * a_normal.y +
                             cross(modelX, modelY) * a_normal.z);
    v_worldPos = mul(u_model[0], vec4(a_position, 1.0)).xyz;
    v_color0 = a_color0;
    v_texcoord0 = a_texcoord0;
}
