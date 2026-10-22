$input v_worldPos

#include "bgfx_shader.sh"
SAMPLER2D(s_texColor, 0);

void main()
{
    vec3 direction = normalize(v_worldPos);
    vec2 uv = vec2(atan2(direction.z, direction.x) / 6.28318530718 + 0.5,
                   acos(clamp(direction.y, -1.0, 1.0)) / 3.14159265359);
    // Input is an already tone-mapped display-sRGB panorama, like the output
    // of the built-in scene shader. Do not light or tone-map it a second time.
#ifdef DEMI_ANALYTIC_DEPTH_OUTPUT
    gl_FragData[0] = vec4(texture2D(s_texColor, uv).rgb, 1.0);
    gl_FragData[1] = vec4(1.0, 0.0, 0.0, 1.0);
#else
    gl_FragColor = vec4(texture2D(s_texColor, uv).rgb, 1.0);
#endif
}
