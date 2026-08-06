$input a_position, a_texcoord0, a_color0
$output v_color0, v_texcoord0

#include <bgfx_shader.sh>

void main()
{
    vec4 position = mul(u_viewProj, vec4(a_position.xy, 0.0, 1.0));
    gl_Position = vec4(position.x, position.y, 0.0, 1.0);
    v_texcoord0 = a_texcoord0;
    v_color0 = a_color0;
}
