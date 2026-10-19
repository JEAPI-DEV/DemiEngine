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

// Snapshot derivatives while every fragment lane is live. Later coverage
// rejection must not change texture mip selection or the normal-map frame.
struct PbrTextureCoordinate
{
    vec2 uv;
    vec2 dx;
    vec2 dy;
};

struct PbrSamplingCoordinates
{
    PbrTextureCoordinate first;
    PbrTextureCoordinate second;
    PbrTextureCoordinate third;
    vec3 weights;
};

PbrTextureCoordinate pbrTextureCoordinate(vec2 uv)
{
    PbrTextureCoordinate result;
    result.uv = uv;
    result.dx = dFdx(uv);
    result.dy = dFdy(uv);
    return result;
}

PbrSamplingCoordinates pbrSamplingCoordinates(vec2 uv, vec3 localPosition,
                                              vec3 localNormal)
{
    PbrSamplingCoordinates result;
    if (u_pbrMapFlags2.z < 0.5)
    {
        result.first = pbrTextureCoordinate(uv * u_pbrSurface.y);
        result.second = result.first;
        result.third = result.first;
        result.weights = vec3(1.0, 0.0, 0.0);
        return result;
    }
    result.first = pbrTextureCoordinate(pbrProjectionUv(0, localPosition, localNormal));
    result.second = pbrTextureCoordinate(pbrProjectionUv(1, localPosition, localNormal));
    result.third = pbrTextureCoordinate(pbrProjectionUv(2, localPosition, localNormal));
    result.weights = pbrProjectionWeights(localNormal);
    return result;
}

vec4 pbrReadMap(sampler2D mapSampler, PbrTextureCoordinate coordinate, bool encodedColor)
{
    vec4 value = texture2DGrad(mapSampler, coordinate.uv, coordinate.dx, coordinate.dy);
    if (encodedColor)
    {
        value.rgb = decodeColor(value.rgb);
    }
    return value;
}

vec4 pbrSampleMap(sampler2D mapSampler, bool encodedColor,
                  PbrSamplingCoordinates coordinates)
{
    if (u_pbrMapFlags2.z < 0.5)
    {
        return pbrReadMap(mapSampler, coordinates.first, encodedColor);
    }
    return pbrReadMap(mapSampler, coordinates.first, encodedColor) * coordinates.weights.x +
           pbrReadMap(mapSampler, coordinates.second, encodedColor) * coordinates.weights.y +
           pbrReadMap(mapSampler, coordinates.third, encodedColor) * coordinates.weights.z;
}

vec3 pbrReadTangentNormal(sampler2D mapSampler, PbrTextureCoordinate coordinate, float strength)
{
    vec3 value = texture2DGrad(mapSampler, coordinate.uv, coordinate.dx, coordinate.dy).xyz * 2.0 - 1.0;
    value.y *= u_pbrSurface.w;
    value.xy *= strength;
    value.z = mix(1.0, value.z, min(strength, 1.0));
    return pbrNormalize(value, vec3(0.0, 0.0, 1.0));
}

vec3 pbrTangentNormal(PbrTextureCoordinate coordinate)
{
    vec3 normal = vec3(0.0, 0.0, 1.0);
    if (u_pbrMapFlags.x > 0.5)
    {
        normal = pbrReadTangentNormal(s_pbrNormal, coordinate, u_pbrSurface.x);
    }
    if (u_pbrMapFlags2.x > 0.5 && u_pbrSurface.z > 0.0)
    {
        vec3 detail = pbrReadTangentNormal(s_pbrDetailNormal, coordinate, u_pbrSurface.z);
        normal = pbrNormalize(vec3(normal.xy + detail.xy, normal.z * detail.z),
                              vec3(0.0, 0.0, 1.0));
    }
    return normal;
}

// Derivative-based cotangent frames support inline, imported and skinned
// meshes without assuming tangent attributes. Row scaling avoids dependence
// on world magnitude or pixel footprint, including translated terrain.
vec3 pbrPerturbNormal(vec3 normal, vec3 worldDx, vec3 worldDy, PbrTextureCoordinate coordinate)
{
    vec2 uvDx = coordinate.dx;
    vec2 uvDy = coordinate.dy;
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
    vec3 mapped = pbrTangentNormal(coordinate);
    return pbrNormalize(tangent * inverseLength * mapped.x +
                         bitangent * inverseLength * mapped.y + normal * mapped.z, normal);
}

vec3 pbrSurfaceNormal(vec3 normal, vec3 worldDx, vec3 worldDy,
                      PbrSamplingCoordinates coordinates)
{
    if (u_pbrMapFlags.x < 0.5 && u_pbrMapFlags2.x < 0.5)
    {
        return normal;
    }
    if (u_pbrMapFlags2.z < 0.5)
    {
        return pbrPerturbNormal(normal, worldDx, worldDy, coordinates.first);
    }
    return pbrNormalize(
        pbrPerturbNormal(normal, worldDx, worldDy, coordinates.first) * coordinates.weights.x +
        pbrPerturbNormal(normal, worldDx, worldDy, coordinates.second) * coordinates.weights.y +
        pbrPerturbNormal(normal, worldDx, worldDy, coordinates.third) * coordinates.weights.z,
        normal);
}
