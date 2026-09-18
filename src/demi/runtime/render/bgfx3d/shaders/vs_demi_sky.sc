$input a_position
$output v_worldPos

#include "bgfx_shader.sh"

void main()
{
    vec4 clip = mul(u_modelViewProj, vec4(a_position, 1.0));
    gl_Position = vec4(clip.xy, clip.w, clip.w);
    v_worldPos = a_position;
}
