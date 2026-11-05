$input v_color0, v_texcoord0
#include "bgfx_shader.sh"
SAMPLER2D(s_tex, 0);
uniform vec4 u_fogMap;
uniform vec4 u_fogFocus;
uniform vec4 u_fogData;

vec3 boundary(float index) {
    // Neighbour lookups are at most two entries outside the ring. Avoid
    // floating-point modulo: at exact multiples the backend can round the
    // quotient down and sample the unused texel after the polygon instead.
    float wrapped = index;
    if (wrapped < 0.0) {
        wrapped += u_fogData.z;
    } else if (wrapped >= u_fogData.z) {
        wrapped -= u_fogData.z;
    }
    return texture2D(s_tex, vec2((wrapped + 0.5) / u_fogData.x, 0.5 / u_fogData.y)).xyz;
}
float discovered(vec2 cell) {
    cell = clamp(cell, vec2(0.0), u_fogMap.zw - vec2(1.0));
    return texture2D(s_tex, (cell + vec2(0.5, 1.5)) / u_fogData.xy).x;
}
float edgeDistanceSquared(vec2 p, vec2 a, vec2 b) {
    vec2 delta = b - a;
    float t = clamp(dot(p - a, delta) / max(dot(delta, delta), 0.00000001), 0.0, 1.0);
    vec2 offset = p - a - t * delta;
    return dot(offset, offset);
}
void main() {
    vec2 world = vec2(v_texcoord0.x, 1.0 - v_texcoord0.y) * u_fogMap.xy;
    // Keep coverage to one drawable pixel at any camera zoom or window size.
    // Evaluate derivatives before divergent angle/edge selection below.
    float aaWidth = max(max(length(dFdx(world)), length(dFdy(world))), 0.000001);
    vec2 cell = world / u_fogMap.xy * u_fogMap.zw - vec2(0.5);
    vec2 base = floor(cell);
    vec2 fraction = fract(cell);
    float memory = mix(mix(discovered(base), discovered(base + vec2(1.0, 0.0)), fraction.x),
                       mix(discovered(base + vec2(0.0, 1.0)), discovered(base + vec2(1.0)), fraction.x), fraction.y);
    float darkness = 1.0 - memory * (1.0 - u_fogData.w);
    vec2 samplePosition = world - u_fogFocus.xy;
    float distance = length(samplePosition);
    float visibility = 0.0;
    if (distance < u_fogFocus.z && u_fogData.z >= 3.0) {
        float angle = mod(atan2(samplePosition.y, samplePosition.x) + 6.28318530718, 6.28318530718);
        float low = 0.0;
        float high = u_fogData.z;
        for (int step = 0; step < 13; ++step) {
            if (low < high) {
                float middle = floor((low + high) * 0.5);
                if (boundary(middle).x <= angle) {
                    low = middle + 1.0;
                } else {
                    high = middle;
                }
            }
        }
        float first = low - 1.0;
        vec2 a = boundary(first).yz;
        vec2 b = boundary(first + 1.0).yz;
        vec2 edge = b - a;
        if (dot(edge, edge) > 0.000000000001) {
            float side = a.x * b.y - a.y * b.x - samplePosition.x * edge.y + samplePosition.y * edge.x;
            float nearest = min(edgeDistanceSquared(samplePosition, a, b),
                                min(edgeDistanceSquared(samplePosition, boundary(first - 1.0).yz, a),
                                    edgeDistanceSquared(samplePosition, b, boundary(first + 2.0).yz)));
            // Analytic pixel coverage. No world-space blur across wall faces.
            float coverage = clamp(0.5 + sqrt(nearest) * (side >= 0.0 ? 1.0 : -1.0) / aaWidth, 0.0, 1.0);
            float fade = clamp((u_fogFocus.z - distance) / u_fogFocus.w, 0.0, 1.0);
            visibility = coverage * fade * fade * (3.0 - 2.0 * fade);
        }
    }
    gl_FragColor = vec4(v_color0.rgb, v_color0.a * darkness * (1.0 - visibility));
}
