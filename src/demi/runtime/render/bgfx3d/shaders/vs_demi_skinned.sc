$input a_position, a_normal, a_color0, a_texcoord0, a_indices, a_weight
$output v_color0, v_normal, v_texcoord0, v_worldPos

#include "bgfx_shader.sh"

uniform mat4 u_skinMatrices[128];
uniform mat4 u_skinImport;

void main()
{
    // Imported weights often use only one or two influences. Zero-weight rows
    // contribute nothing; avoid their palette loads and matrix arithmetic.
    mat4 skin = u_skinMatrices[int(a_indices.x)] * a_weight.x;
    if (a_weight.y != 0.0)
        skin += u_skinMatrices[int(a_indices.y)] * a_weight.y;
    if (a_weight.z != 0.0)
        skin += u_skinMatrices[int(a_indices.z)] * a_weight.z;
    if (a_weight.w != 0.0)
        skin += u_skinMatrices[int(a_indices.w)] * a_weight.w;
    // The CPU reference applies import translation after the weighted xyz sum,
    // even when source weights do not sum to exactly one.
    vec3 posed = mul(skin, vec4(a_position, 1.0)).xyz;
    vec4 position = mul(u_skinImport, vec4(posed, 1.0));
    gl_Position = mul(u_modelViewProj, position);
    v_worldPos = mul(u_model[0], position).xyz;
    mat4 normalTransform = mul(u_model[0], mul(u_skinImport, skin));
    vec3 x = normalTransform[0].xyz;
    vec3 y = normalTransform[1].xyz;
    vec3 z = normalTransform[2].xyz;
    float handedness = dot(x, cross(y, z)) < 0.0 ? -1.0 : 1.0;
    v_normal = handedness * (cross(y, z) * a_normal.x
                          + cross(z, x) * a_normal.y
                          + cross(x, y) * a_normal.z);
    // Collapsed/singular joint blends have no unique normal direction.
    if (dot(v_normal, v_normal) < 1e-12)
        v_normal = vec3(0.0, 1.0, 0.0);
    v_color0 = a_color0;
    v_texcoord0 = a_texcoord0;
}
