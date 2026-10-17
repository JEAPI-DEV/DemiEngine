$input v_waterNormal, v_waterLocal, v_waterRelative, v_waterData

#include "bgfx_shader.sh"
#include "demi_water_optics.sh"
SAMPLER2D(s_waterSceneColor, 0);
SAMPLER2D(s_waterSceneDepth, 1);
SAMPLER2D(s_waterEnvironment, 2);
uniform vec4 u_waterCamera;
uniform vec4 u_waterForward;
uniform vec4 u_waterRight;
uniform vec4 u_waterUp;
uniform vec4 u_waterCapture;
uniform vec4 u_waterProjection;
uniform vec4 u_waterLocalCamera;
uniform vec4 u_waterShallow;
uniform vec4 u_waterDeep;
uniform vec4 u_waterOptics;
uniform vec4 u_waterWaves;
uniform vec4 u_waterFoam;
uniform vec4 u_waterFoamColor;
uniform vec4 u_waterLightDirection;
uniform vec4 u_waterLightColor;
uniform vec4 u_waterAmbient;

vec3 waterDecode(vec3 color)
{
    return mix(color / 12.92, pow((color + 0.055) / 1.055, vec3(2.4)),
        step(vec3(0.04045), color));
}

vec3 waterEncode(vec3 color)
{
    color = max(color, vec3(0.0));
    return mix(color * 12.92, 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055,
        step(vec3(0.0031308), color));
}

vec3 waterAnimatedNormal(vec3 normal, vec2 gradient, vec3 relativePosition, vec3 localPosition)
{
    vec3 dx = dFdx(relativePosition);
    vec3 dy = dFdy(relativePosition);
    vec2 uvDx = dFdx(localPosition.xz);
    vec2 uvDy = dFdy(localPosition.xz);
    vec3 perpendicularY = cross(dy, normal);
    vec3 perpendicularX = cross(normal, dx);
    vec3 tangent = perpendicularY * uvDx.x + perpendicularX * uvDy.x;
    vec3 bitangent = perpendicularY * uvDx.y + perpendicularX * uvDy.y;
    float squaredLength = max(dot(tangent, tangent), dot(bitangent, bitangent));
    if (squaredLength <= 0.000000000001)
    {
        return normal;
    }
    float scale = inversesqrt(squaredLength);
    return normalize(normal - tangent * (scale * gradient.x) - bitangent * (scale * gradient.y));
}

vec3 waterEnvironment(vec3 direction, float roughness)
{
    if (u_waterCapture.w < 0.5)
    {
        return u_waterAmbient.rgb;
    }
    vec2 uv = vec2(atan2(direction.z, direction.x) / 6.28318530718 + 0.5,
        acos(clamp(direction.y, -1.0, 1.0)) / 3.14159265359);
    // Existing environment panoramas carry display-sRGB, like the scene image.
    return waterDecode(texture2DLod(s_waterEnvironment, uv, roughness * 6.0).rgb);
}

void main()
{
    float time = u_waterFoam.z;
    vec2 flow = v_waterData.yz;
    float flowLength = length(flow);
    vec2 direction = flowLength > 0.0001 ? flow / flowLength : vec2(1.0, 0.0);
    vec2 secondary = vec2(direction.x * 0.6 - direction.y * 0.8,
        direction.x * 0.8 + direction.y * 0.6);
    vec2 position = v_waterLocal.xz - flow * time;
    float frequency = 6.28318530718 / max(u_waterWaves.y, 0.0001);
    float phase = dot(position, direction) * frequency - time * u_waterWaves.z * frequency;
    float secondPhase = dot(position, secondary) * frequency * 1.73 +
        time * u_waterWaves.z * frequency * 0.73;
    float shoreFade = smoothstep(0.0, max(u_waterWaves.y * 0.25, 0.0001), v_waterData.x);
    vec2 gradient = (direction * cos(phase) + secondary * (0.35 * 1.73 * cos(secondPhase))) *
        (u_waterWaves.x * frequency * u_waterWaves.w * shoreFade);
    vec3 baseNormal = normalize(v_waterNormal);
    vec3 normal = waterAnimatedNormal(baseNormal, gradient, v_waterRelative, v_waterLocal);
    float viewLength = max(length(v_waterRelative), 0.0001);
    vec3 ray = u_waterProjection.w > 0.5 ? v_waterRelative / viewLength : u_waterForward.xyz;
    bool below = dot(ray, baseNormal) > 0.0;
    if (dot(ray, normal) > 0.0)
    {
        normal = -normal;
    }
    float surfaceDepth = dot(v_waterRelative, u_waterForward.xyz);
    vec2 uv = gl_FragCoord.xy * u_waterCapture.xy;
    if (u_waterCapture.z > 0.5)
    {
        uv.y = 1.0 - uv.y;
    }
    vec2 border = u_waterCapture.xy * 0.5;
    uv = clamp(uv, border, vec2(1.0) - border);
    float sceneDepth = texture2D(s_waterSceneDepth, uv).r * u_waterForward.w;
    float path = waterRayPath(sceneDepth, surfaceDepth, dot(ray, u_waterForward.xyz));
    vec3 refracted = refract(ray, normal, below ? 1.333 : 0.75018755);
    bool totalReflection = dot(refracted, refracted) < 0.0001;
    vec3 deflection = refracted - ray;
    float projectionDepth = u_waterProjection.w > 0.5 ?
        max(sceneDepth, u_waterProjection.z) : 1.0;
    vec2 offset = vec2(dot(deflection, u_waterRight.xyz), dot(deflection, u_waterUp.xyz)) *
        u_waterProjection.xy * (path / projectionDepth);
    if (u_waterCapture.z > 0.5)
    {
        offset.y = -offset.y;
    }
    vec2 refractedUv = clamp(uv + offset, border, vec2(1.0) - border);
    float refractedDepth = texture2D(s_waterSceneDepth, refractedUv).r * u_waterForward.w;
    // Foreground geometry must never smear across the water silhouette.
    if (refractedDepth <= surfaceDepth || totalReflection)
    {
        refractedUv = uv;
        refractedDepth = sceneDepth;
    }
    path = waterRayPath(refractedDepth, surfaceDepth, dot(ray, u_waterForward.xyz));
    float localMetric = u_waterProjection.w > 0.5 ?
        length(v_waterLocal - u_waterLocalCamera.xyz) / viewLength : u_waterLocalCamera.w;
    float localPath = path * localMetric;
    float shallow = waterDepthContribution(localPath, u_waterOptics.x);
    vec3 scatter = mix(u_waterDeep.rgb, u_waterShallow.rgb, shallow);
    float opacity = waterSurfaceOpacity(shallow, u_waterShallow.a, u_waterDeep.a, below);
    vec3 background = waterDecode(texture2D(s_waterSceneColor, refractedUv).rgb);
    vec3 transmitted = mix(background, scatter, opacity);
    float fresnel = totalReflection ? 1.0 : waterFresnel(dot(-ray, normal));
    vec3 reflection = waterEnvironment(reflect(ray, normal), u_waterOptics.y);
    vec3 color = mix(transmitted, reflection, fresnel);
    vec3 light = -u_waterLightDirection.xyz;
    light /= max(length(light), 0.0001);
    vec3 halfway = light - ray;
    halfway /= max(length(halfway), 0.0001);
    float highlight = pow(max(dot(normal, halfway), 0.0),
        mix(256.0, 4.0, u_waterOptics.y * u_waterOptics.y));
    color += u_waterLightColor.rgb * u_waterLightDirection.w * highlight * fresnel;
    float foamDepth = below ? v_waterData.x : min(v_waterData.x, localPath);
    float foamBand = waterShoreFoam(foamDepth, u_waterFoam.x);
    float streak = 0.65 + 0.35 * sin(phase * 0.37 + secondPhase * 0.23);
    float foam = clamp(foamBand * u_waterFoam.y * streak * u_waterFoamColor.a, 0.0, 1.0);
    color = mix(color, u_waterFoamColor.rgb, foam);
    // This is the complete optical composite over immutable opaque colour.
    // Ordinary alpha blending here would apply the background a second time.
    gl_FragColor = vec4(waterEncode(color), 1.0);
}
