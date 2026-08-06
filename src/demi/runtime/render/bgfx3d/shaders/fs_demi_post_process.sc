$input v_color0, v_texcoord0

#include "bgfx_shader.sh"

SAMPLER2D(s_postColor, 0);
uniform vec4 u_colorAdjust;
uniform vec4 u_tint;
uniform vec4 u_bloomFade;
uniform vec4 u_fadeColor;

void main()
{
    vec2 uv = v_texcoord0;
#if BGFX_SHADER_LANGUAGE_GLSL
    uv.y = 1.0 - uv.y;
#endif
    vec4 source = texture2D(s_postColor, uv) * v_color0;
    vec3 color = source.rgb * exp2(u_colorAdjust.x);
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, u_colorAdjust.z);
    color = (color - 0.5) * u_colorAdjust.y + 0.5;
    float bloomMask = max(luminance - u_bloomFade.y, 0.0);
    color += color * bloomMask * u_bloomFade.x;
    float edge = smoothstep(0.2, 0.75, length(v_texcoord0 - vec2(0.5)));
    color *= 1.0 - edge * u_colorAdjust.w;
    color *= u_tint.rgb;
    color = mix(color, u_fadeColor.rgb,
                clamp(u_bloomFade.z * u_fadeColor.a, 0.0, 1.0));
    gl_FragColor = vec4(color, source.a);
}
