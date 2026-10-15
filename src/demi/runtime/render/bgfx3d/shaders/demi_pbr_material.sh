SAMPLER2D(s_pbrNormal, 3);
SAMPLER2D(s_pbrRoughness, 4);
SAMPLER2D(s_pbrMetallic, 5);
SAMPLER2D(s_pbrOcclusion, 6);
SAMPLER2D(s_pbrDetailNormal, 7);
SAMPLER2D(s_pbrEmissive, 8);
// Normal, roughness, metallic, ambient occlusion map presence.
uniform vec4 u_pbrMapFlags;
// Detail normal, emissive, triplanar projection, encoded emissive RGB.
uniform vec4 u_pbrMapFlags2;
// Normal strength, material tiling, detail strength, normal-map Y sign.
uniform vec4 u_pbrSurface;
// MeshRenderer.size, then biome repetitions per terrain-local unit.
uniform vec4 u_pbrLocalScale;

vec3 pbrNormalize(vec3 value, vec3 fallback)
{
    float extent = max(abs(value.x), max(abs(value.y), abs(value.z)));
    if (!(extent > 0.0))
    {
        return fallback;
    }
    vec3 scaled = value / extent;
    return scaled * inversesqrt(dot(scaled, scaled));
}

vec3 pbrLocalNormal(vec3 localNormal)
{
    vec3 size = u_pbrLocalScale.xyz;
    float extent = max(abs(size.x), max(abs(size.y), abs(size.z)));
    if (extent > 0.0)
    {
        size /= extent;
    }
    vec3 cofactor = vec3(size.y * size.z, size.z * size.x, size.x * size.y);
    float handedness = size.x * size.y * size.z < 0.0 ? -1.0 : 1.0;
    return pbrNormalize(localNormal * cofactor * handedness, vec3(0.0, 1.0, 0.0));
}

vec3 pbrProjectionWeights(vec3 localNormal)
{
    vec3 weights = abs(pbrLocalNormal(localNormal));
    return weights / (weights.x + weights.y + weights.z);
}

vec2 pbrProjectionUv(int axis, vec3 localPosition, vec3 localNormal)
{
    vec3 position = localPosition * u_pbrLocalScale.xyz *
                    (u_pbrSurface.y * u_pbrLocalScale.w);
    vec3 normal = pbrLocalNormal(localNormal);
    if (axis == 0)
    {
        return vec2(normal.x < 0.0 ? position.z : -position.z, position.y);
    }
    if (axis == 1)
    {
        return vec2(position.x, normal.y < 0.0 ? -position.z : position.z);
    }
    return vec2(normal.z < 0.0 ? -position.x : position.x, position.y);
}

vec4 pbrReadMap(sampler2D mapSampler, vec2 uv, bool encodedColor)
{
    vec4 value = texture2D(mapSampler, uv);
    if (encodedColor)
    {
        value.rgb = decodeColor(value.rgb);
    }
    return value;
}

vec4 pbrSampleMap(sampler2D mapSampler, bool encodedColor, vec2 uv,
                  vec3 localPosition, vec3 localNormal)
{
    if (u_pbrMapFlags2.z < 0.5)
    {
        return pbrReadMap(mapSampler, uv * u_pbrSurface.y, encodedColor);
    }
    vec3 weights = pbrProjectionWeights(localNormal);
    return pbrReadMap(mapSampler, pbrProjectionUv(0, localPosition, localNormal), encodedColor) * weights.x +
           pbrReadMap(mapSampler, pbrProjectionUv(1, localPosition, localNormal), encodedColor) * weights.y +
           pbrReadMap(mapSampler, pbrProjectionUv(2, localPosition, localNormal), encodedColor) * weights.z;
}

vec3 pbrReadTangentNormal(sampler2D mapSampler, vec2 uv, float strength)
{
    vec3 value = texture2D(mapSampler, uv).xyz * 2.0 - 1.0;
    value.y *= u_pbrSurface.w;
    value.xy *= strength;
    value.z = mix(1.0, value.z, min(strength, 1.0));
    return pbrNormalize(value, vec3(0.0, 0.0, 1.0));
}

vec3 pbrTangentNormal(vec2 uv)
{
    vec3 normal = vec3(0.0, 0.0, 1.0);
    if (u_pbrMapFlags.x > 0.5)
    {
        normal = pbrReadTangentNormal(s_pbrNormal, uv, u_pbrSurface.x);
    }
    if (u_pbrMapFlags2.x > 0.5 && u_pbrSurface.z > 0.0)
    {
        vec3 detail = pbrReadTangentNormal(s_pbrDetailNormal, uv, u_pbrSurface.z);
        normal = pbrNormalize(vec3(normal.xy + detail.xy, normal.z * detail.z),
                              vec3(0.0, 0.0, 1.0));
    }
    return normal;
}

// Derivative-based cotangent frames support inline, imported and skinned
// meshes without assuming tangent attributes. Row scaling avoids dependence
// on world magnitude or pixel footprint, including translated terrain.
vec3 pbrPerturbNormal(vec3 normal, vec3 worldDx, vec3 worldDy, vec2 uv)
{
    vec2 uvDx = dFdx(uv);
    vec2 uvDy = dFdy(uv);
    float scaleX = max(abs(worldDx.x), max(abs(worldDx.y), abs(worldDx.z)));
    float scaleY = max(abs(worldDy.x), max(abs(worldDy.y), abs(worldDy.z)));
    if (!(scaleX > 0.0) || !(scaleY > 0.0))
    {
        return normal;
    }
    vec3 dx = worldDx / scaleX;
    vec3 dy = worldDy / scaleY;
    uvDx /= scaleX;
    uvDy /= scaleY;
    vec3 perpendicularY = cross(dy, normal);
    vec3 perpendicularX = cross(normal, dx);
    vec3 tangent = perpendicularY * uvDx.x + perpendicularX * uvDy.x;
    vec3 bitangent = perpendicularY * uvDx.y + perpendicularX * uvDy.y;
    float extent = max(max(abs(tangent.x), max(abs(tangent.y), abs(tangent.z))),
                       max(abs(bitangent.x), max(abs(bitangent.y), abs(bitangent.z))));
    if (!(extent > 0.0))
    {
        return normal;
    }
    tangent /= extent;
    bitangent /= extent;
    float inverseLength = inversesqrt(max(dot(tangent, tangent), dot(bitangent, bitangent)));
    vec3 mapped = pbrTangentNormal(uv);
    return pbrNormalize(tangent * inverseLength * mapped.x +
                         bitangent * inverseLength * mapped.y + normal * mapped.z, normal);
}

vec3 pbrSurfaceNormal(vec3 normal, vec3 worldDx, vec3 worldDy, vec2 uv,
                      vec3 localPosition, vec3 localNormal)
{
    if (u_pbrMapFlags.x < 0.5 && u_pbrMapFlags2.x < 0.5)
    {
        return normal;
    }
    if (u_pbrMapFlags2.z < 0.5)
    {
        return pbrPerturbNormal(normal, worldDx, worldDy, uv * u_pbrSurface.y);
    }
    vec3 weights = pbrProjectionWeights(localNormal);
    return pbrNormalize(
        pbrPerturbNormal(normal, worldDx, worldDy, pbrProjectionUv(0, localPosition, localNormal)) * weights.x +
        pbrPerturbNormal(normal, worldDx, worldDy, pbrProjectionUv(1, localPosition, localNormal)) * weights.y +
        pbrPerturbNormal(normal, worldDx, worldDy, pbrProjectionUv(2, localPosition, localNormal)) * weights.z,
        normal);
}
