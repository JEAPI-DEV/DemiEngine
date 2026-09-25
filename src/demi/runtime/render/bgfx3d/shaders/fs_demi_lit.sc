$input v_color0, v_normal, v_texcoord0, v_worldPos

#include "bgfx_shader.sh"

SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_shadowMap, 1);
uniform vec4 u_shadowX;
uniform vec4 u_shadowY;
uniform vec4 u_shadowZ;
// x: disabled=0, receiver=1, depth pass=-1; y: bias; z: texel; w: flip V.
uniform vec4 u_shadowParams;
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

void main()
{
    vec4 albedo = texture2D(s_texColor, v_texcoord0) * v_color0 * u_tint;
    if (albedo.a < u_alphaCutoff.x)
        discard;

    vec4 worldPosition = vec4(v_worldPos, 1.0);
    float shadowDepth = dot(u_shadowZ, worldPosition);
    if (u_shadowParams.x < -0.5)
    {
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
    if (u_shadowParams.x > 0.5)
    {
        vec2 shadowUv = vec2(dot(u_shadowX, worldPosition), dot(u_shadowY, worldPosition));
        if (u_shadowParams.w > 0.5) shadowUv.y = 1.0 - shadowUv.y;
        // Compare each PCF tap against the receiver plane at that texel,
        // rather than against the depth at the centre of a sloped footprint.
        vec2 uvDx = dFdx(shadowUv);
        vec2 uvDy = dFdy(shadowUv);
        vec2 depthDerivative = vec2(dFdx(shadowDepth), dFdy(shadowDepth));
        float determinant = uvDx.x * uvDy.y - uvDx.y * uvDy.x;
        vec2 depthGradient = vec2(0.0);
        if (abs(determinant) > 0.000000000001)
            depthGradient = vec2(uvDy.y * depthDerivative.x - uvDx.y * depthDerivative.y,
                                 uvDx.x * depthDerivative.y - uvDy.x * depthDerivative.x) / determinant;
        if (shadowUv.x >= 0.0 && shadowUv.x <= 1.0 &&
            shadowUv.y >= 0.0 && shadowUv.y <= 1.0 && shadowDepth >= 0.0 && shadowDepth <= 1.0)
        {
            visibility = 0.0;
            float compareDepth = shadowDepth - u_shadowParams.y * (2.0 - diffuse);
            for (int y = -1; y <= 1; ++y)
                for (int x = -1; x <= 1; ++x)
                {
                    vec2 tap = (floor(shadowUv / u_shadowParams.z) + vec2(float(x),float(y)) + 0.5) * u_shadowParams.z;
                    vec4 encoded = texture2D(s_shadowMap, tap);
                    float stored = dot(encoded, vec4(1.0, 1.0/255.0, 1.0/65025.0, 1.0/16581375.0));
                    visibility += step(compareDepth + dot(depthGradient, tap - shadowUv), stored) / 9.0;
                }
        }
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
