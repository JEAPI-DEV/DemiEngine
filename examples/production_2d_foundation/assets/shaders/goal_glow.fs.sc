$input v_color0, v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);
uniform vec4 u_effect_color;
uniform vec4 u_strength;

void main()
{
    vec4 base = texture2D(s_tex, v_texcoord0) * v_color0;
    gl_FragColor = vec4(mix(base.rgb, u_effect_color.rgb, u_strength.x), base.a);
}
