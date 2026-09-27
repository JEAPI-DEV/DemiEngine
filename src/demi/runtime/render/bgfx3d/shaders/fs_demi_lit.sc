$input v_color0, v_normal, v_texcoord0, v_worldPos

#include "bgfx_shader.sh"

SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_shadowMap, 1);
uniform vec4 u_shadowX[4];
uniform vec4 u_shadowY[4];
uniform vec4 u_shadowZ[4];
// Per cascade: normalized bias, nominal near depth, far depth, reserved.
uniform vec4 u_shadowParams[4];
// Phase (off/receive/cast), active cast index, cascade count, blend fraction.
uniform vec4 u_shadowConfig;
// Atlas columns/rows, signed texel size (negative = hard), local flip V.
uniform vec4 u_shadowAtlas;
uniform vec4 u_shadowSplits;
uniform vec4 u_shadowCamera;
uniform vec4 u_lightDirection;
uniform vec4 u_lightColor;
uniform vec4 u_ambientColor;
uniform vec4 u_tint;
uniform vec4 u_alphaCutoff;
uniform vec4 u_debugMode;
#ifndef DEMI_DIRECTIONAL_ONLY
uniform vec4 u_pointPositionRange[4];
uniform vec4 u_pointColorIntensity[4];
uniform vec4 u_spotPositionRange[4];
uniform vec4 u_spotDirectionOuter[4];
uniform vec4 u_spotColorIntensity[4];
uniform vec4 u_spotInner[4];
#endif

// Base-color textures and authored colors use display sRGB. The 3D target is
// an ordinary UNORM target (also used by the editor), so transfer conversion
// belongs here, not on the backbuffer where it would affect HUD colors too.
vec3 decodeColor(vec3 color)
{
    color = max(color, vec3(0.0));
    return mix(color / 12.92, pow((color + 0.055) / 1.055, vec3(2.4)),
               step(vec3(0.04045), color));
}

vec3 encodeColor(vec3 color)
{
    color = max(color, vec3(0.0));
    return mix(color * 12.92, 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055,
               step(vec3(0.0031308), color));
}

float compareShadowTap(int cascade, vec2 tap, float receiverDepth)
{
    float texel = abs(u_shadowAtlas.z);
    tap = clamp(tap, vec2(texel * 0.5), vec2(1.0 - texel * 0.5));
    vec2 tile = vec2(mod(float(cascade), u_shadowAtlas.x), floor(float(cascade) / u_shadowAtlas.x));
    if (u_shadowAtlas.w < 0.5)
        tile.y = u_shadowAtlas.y - 1.0 - tile.y;
    vec2 atlasUv = (tile + tap) / u_shadowAtlas.xy;
    vec4 encoded = texture2D(s_shadowMap, atlasUv);
    float stored = dot(encoded, vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0));
    return step(receiverDepth, stored);
}

float cascadeVisibility(int cascade, vec4 worldPosition, vec3 worldDx, vec3 worldDy, float diffuse)
{
    vec2 uv = vec2(dot(u_shadowX[cascade], worldPosition), dot(u_shadowY[cascade], worldPosition));
    if (u_shadowAtlas.w > 0.5) uv.y = 1.0 - uv.y;
    float depth = dot(u_shadowZ[cascade], worldPosition);
    // Receiver-plane correction prevents broad PCF footprints self-shadowing
    // sloped surfaces. Compute derivatives before coverage branches.
    vec2 uvDx = vec2(dot(u_shadowX[cascade].xyz, worldDx), dot(u_shadowY[cascade].xyz, worldDx));
    vec2 uvDy = vec2(dot(u_shadowX[cascade].xyz, worldDy), dot(u_shadowY[cascade].xyz, worldDy));
    if (u_shadowAtlas.w > 0.5) { uvDx.y = -uvDx.y; uvDy.y = -uvDy.y; }
    vec2 dz = vec2(dot(u_shadowZ[cascade].xyz, worldDx), dot(u_shadowZ[cascade].xyz, worldDy));
    float determinant = uvDx.x * uvDy.y - uvDx.y * uvDy.x;
    vec2 gradient = vec2(0.0);
    if (abs(determinant) > 0.000000000001)
        gradient = vec2(uvDy.y * dz.x - uvDx.y * dz.y,
                        uvDx.x * dz.y - uvDy.x * dz.x) / determinant;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || depth < 0.0 || depth > 1.0)
        return 1.0;
    float texel = abs(u_shadowAtlas.z);
    float compareDepth = depth - u_shadowParams[cascade].x * (2.0 - diffuse);
    if (u_shadowAtlas.z < 0.0)
    {
        vec2 tap = (floor(uv / texel) + 0.5) * texel;
        return compareShadowTap(cascade, tap, compareDepth + dot(gradient, tap - uv));
    }
    // Continuous 3x3 bilinear PCF, regrouped as 16 weighted comparisons.
    // Never interpolate packed depth bytes themselves or sample another tile.
    vec2 position = uv / texel - 0.5;
    vec2 base = floor(position);
    vec2 fraction = fract(position);
    vec4 weightsX = vec4(1.0 - fraction.x, 1.0, 1.0, fraction.x);
    vec4 weightsY = vec4(1.0 - fraction.y, 1.0, 1.0, fraction.y);
    float visibility = 0.0;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
        {
            vec2 tap = clamp((base + vec2(float(x - 1), float(y - 1)) + 0.5) * texel,
                             vec2(texel * 0.5), vec2(1.0 - texel * 0.5));
            visibility += weightsX[x] * weightsY[y] * compareShadowTap(cascade, tap,
                compareDepth + dot(gradient, tap - uv)) / 9.0;
        }
    return visibility;
}

void main()
{
    vec4 albedo = texture2D(s_texColor, v_texcoord0) * v_color0 * u_tint;
    if (albedo.a < u_alphaCutoff.x)
        discard;

    vec4 worldPosition = vec4(v_worldPos, 1.0);
    vec3 worldDx = dFdx(v_worldPos);
    vec3 worldDy = dFdy(v_worldPos);
    if (u_shadowConfig.x < -0.5)
    {
        float shadowDepth = dot(u_shadowZ[int(u_shadowConfig.y)], worldPosition);
        vec4 packedDepth = fract(clamp(shadowDepth, 0.0, 0.999999) *
                                 vec4(1.0, 255.0, 65025.0, 16581375.0));
        gl_FragColor = packedDepth - packedDepth.yzww *
                      vec4(1.0/255.0, 1.0/255.0, 1.0/255.0, 0.0);
        return;
    }

    float normalLength = length(v_normal);
    vec3 normal = v_normal / max(normalLength, 0.0001);
    vec3 directionalVector = -u_lightDirection.xyz;
    vec3 directionalDirection = directionalVector /
                                max(length(directionalVector), 0.0001);
    float diffuse = max(dot(normal, directionalDirection), 0.0);
    float visibility = 1.0;
    if (u_shadowConfig.x > 0.5)
    {
        float viewDepth = dot(u_shadowCamera, worldPosition);
        int cascade = 0;
        if (viewDepth > u_shadowSplits.x && u_shadowConfig.z > 1.5) cascade = 1;
        if (viewDepth > u_shadowSplits.y && u_shadowConfig.z > 2.5) cascade = 2;
        if (viewDepth > u_shadowSplits.z && u_shadowConfig.z > 3.5) cascade = 3;
        visibility = cascadeVisibility(cascade, worldPosition, worldDx, worldDy, diffuse);
        float farDepth = u_shadowParams[cascade].z;
        float blendWidth = max((farDepth - u_shadowParams[cascade].y) * u_shadowConfig.w, 0.00001);
        float blend = smoothstep(farDepth - blendWidth, farDepth, viewDepth);
        if (cascade + 1 < int(u_shadowConfig.z))
        {
            if (blend > 0.0)
                visibility = mix(visibility, cascadeVisibility(cascade + 1, worldPosition, worldDx, worldDy, diffuse), blend);
        }
        else
            visibility = mix(visibility, 1.0, blend);
        if (viewDepth < 0.0 || viewDepth > farDepth) visibility = 1.0;
    }
    vec3 lighting = u_ambientColor.rgb +
                    u_lightColor.rgb * diffuse * u_lightDirection.w * visibility;
#ifndef DEMI_DIRECTIONAL_ONLY
    for (int ii = 0; ii < 4; ++ii)
    {
        if (u_pointPositionRange[ii].w > 0.0 &&
            u_pointColorIntensity[ii].w > 0.0)
        {
            vec3 toLight = u_pointPositionRange[ii].xyz - v_worldPos;
            float distanceToLight = length(toLight);
            vec3 lightDirection = toLight / max(distanceToLight, 0.0001);
            float attenuation = max(1.0 - distanceToLight /
                                    u_pointPositionRange[ii].w, 0.0);
            float pointDiffuse = max(dot(normal, lightDirection), 0.0);
            lighting += u_pointColorIntensity[ii].rgb *
                        u_pointColorIntensity[ii].w * pointDiffuse *
                        attenuation * attenuation;
        }

        if (u_spotPositionRange[ii].w > 0.0 &&
            u_spotColorIntensity[ii].w > 0.0)
        {
            vec3 toSpot = u_spotPositionRange[ii].xyz - v_worldPos;
            float distanceToSpot = length(toSpot);
            vec3 lightDirection = toSpot / max(distanceToSpot, 0.0001);
            vec3 spotVector = u_spotDirectionOuter[ii].xyz;
            vec3 spotDirection = spotVector / max(length(spotVector), 0.0001);
            float cone = dot(-lightDirection, spotDirection);
            float coneAmount = smoothstep(u_spotDirectionOuter[ii].w,
                                          u_spotInner[ii].x, cone);
            float spotAttenuation = max(1.0 - distanceToSpot /
                                        u_spotPositionRange[ii].w, 0.0);
            float spotDiffuse = max(dot(normal, lightDirection), 0.0);
            lighting += u_spotColorIntensity[ii].rgb *
                        u_spotColorIntensity[ii].w * spotDiffuse * coneAmount *
                        spotAttenuation * spotAttenuation;
        }
    }
#endif
    if (u_debugMode.x > 0.5 && u_debugMode.x < 1.5)
    {
        gl_FragColor = vec4(normal * 0.5 + 0.5, 1.0);
    }
    else if (u_debugMode.x > 1.5 && u_debugMode.x < 2.5)
    {
        vec2 cell = floor(fract(v_texcoord0) * 10.0);
        float checker = mod(cell.x + cell.y, 2.0);
        vec3 uvColor = mix(vec3(0.08), vec3(0.92), checker);
        if (v_texcoord0.x < 0.0 || v_texcoord0.y < 0.0 ||
            v_texcoord0.x > 1.0 || v_texcoord0.y > 1.0)
            uvColor = vec3(1.0, 0.1, 0.1);
        gl_FragColor = vec4(uvColor, 1.0);
    }
    else if (u_debugMode.x > 2.5 && u_debugMode.x < 3.5)
    {
        gl_FragColor = vec4(vec3(albedo.a), 1.0);
    }
    else if (u_debugMode.x > 3.5 && u_debugMode.x < 4.5)
    {
        gl_FragColor = vec4(lighting, 1.0);
    }
    else if (u_debugMode.x > 4.5 && u_debugMode.x < 5.5)
    {
        gl_FragColor = vec4(0.12, 0.015, 0.0, 0.12);
    }
    else if (u_debugMode.x > 5.5)
    {
        gl_FragColor = u_debugMode.y > 0.5
            ? vec4(0.1, 0.9, 0.2, 1.0)
            : vec4(0.95, 0.15, 0.1, 1.0);
    }
    else
    {
        gl_FragColor = vec4(encodeColor(decodeColor(albedo.rgb) * lighting), albedo.a);
    }
}
