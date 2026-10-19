// Arrays share a layer index, but each channel retains its sampling semantics.
SAMPLER2DARRAY(s_terrainAlbedo, 7);
SAMPLER2DARRAY(s_terrainNormals, 8);
SAMPLER2DARRAY(s_terrainOrmHeight, 9);
SAMPLER2DARRAY(s_terrainDetail, 10);
SAMPLER2DARRAY(s_terrainEmissive, 11);
// Resident array edge and world-space distance where detail normals fade out.
uniform vec4 u_terrainSurface;

#include "demi_terrain_normal_frame.sh"

float terrainMacroNoise(vec3 position)
{
    // Smooth, deterministic local-space modulation; no camera-relative swimming.
    vec3 cell = floor(position);
    vec3 fraction = fract(position);
    fraction = fraction * fraction * (3.0 - 2.0 * fraction);
    float value = 0.0;
    for (int z = 0; z < 2; ++z)
    {
        for (int y = 0; y < 2; ++y)
        {
            for (int x = 0; x < 2; ++x)
            {
                vec3 corner = vec3(float(x), float(y), float(z));
                vec3 weights = mix(vec3(1.0) - fraction, fraction, corner);
                float hash = fract(sin(dot(cell + corner, vec3(12.9898, 78.233, 37.719))) * 43758.5453);
                value += hash * weights.x * weights.y * weights.z;
            }
        }
    }
    return value;
}

vec2 terrainProject(vec3 position, int axis, vec3 normal)
{
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

vec2 terrainWrap(vec2 uv, float mode, float mip)
{
    if (mode > 1.5)
    {
        return 1.0 - abs(mod(uv, 2.0) - 1.0);
    }
    if (mode > 0.5)
    {
        // The array sampler repeats for other layers. Clamp inside the coarser
        // trilinear level so this layer never blends across opposite edges.
        // Floor also covers source images whose resident edge is not a power of two.
        float mipEdge = max(floor(u_terrainSurface.x / exp2(ceil(mip))), 1.0);
        float border = 0.5 / mipEdge;
        return clamp(uv, vec2(border), vec2(1.0 - border));
    }
    return uv;
}

struct TerrainMappedSurface
{
    vec3 color;
    vec3 normal;
    vec3 emissive;
    vec3 properties;
    float occlusion;
    float height;
    float heightStrength;
    float visible;
};

void accumulateTerrainProjection(int axis, float weight,
    vec4 mapping, vec4 strengths, vec4 detail, float detailFade,
    vec3 localPosition, vec3 localNormal, vec3 localDx, vec3 localDy,
    TerrainNormalBasis normalBasis, vec3 receiverNormal,
    inout TerrainMappedSurface result)
{
    if (weight <= 0.0001)
    {
        return;
    }
    vec2 uv = terrainProject(localPosition, axis, localNormal) * mapping.y;
    vec2 uvDx = terrainProject(localDx, axis, localNormal) * mapping.y;
    vec2 uvDy = terrainProject(localDy, axis, localNormal) * mapping.y;
    float footprintSquared = max(dot(uvDx, uvDx), dot(uvDy, uvDy)) *
                             (u_terrainSurface.x * u_terrainSurface.x);
    float mip = 0.5 * log2(max(footprintSquared, 1.0));
    vec3 coordinate = vec3(terrainWrap(uv, detail.y, mip), mapping.x);
    vec4 albedo = texture2DArrayLod(s_terrainAlbedo, coordinate, mip);
    vec4 ormHeight = texture2DArrayLod(s_terrainOrmHeight, coordinate, mip);
#if !(DEMI_PERFORMANCE_DEBUG && DEMI_PERFORMANCE_DEBUG_DISABLE_TERRAIN_NORMALS)
    vec3 mapped = vec3(0.0, 0.0, 1.0);
    if (strengths.x > 0.0)
    {
        mapped = texture2DArrayLod(s_terrainNormals, coordinate, mip).xyz * 2.0 - 1.0;
    }
    mapped.y *= mapping.w;
    mapped.xy *= strengths.x;
    mapped.z = mix(1.0, mapped.z, min(strengths.x, 1.0));
    if (strengths.y * detailFade > 0.0)
    {
        vec3 fine = texture2DArrayLod(s_terrainDetail, coordinate, mip).xyz * 2.0 - 1.0;
        fine.y *= mapping.w;
        float detailStrength = strengths.y * detailFade;
        fine.xy *= detailStrength;
        fine.z = mix(1.0, fine.z, min(detailStrength, 1.0));
        fine = terrainNormalize(fine, vec3(0.0, 0.0, 1.0));
        // Match the ordinary PBR material's whiteout detail composition.
        mapped = terrainNormalize(mapped, vec3(0.0, 0.0, 1.0));
        mapped = terrainNormalize(vec3(mapped.xy + fine.xy, mapped.z * fine.z),
                                  vec3(0.0, 0.0, 1.0));
    }
    if (detail.y > 1.5)
    {
        mapped.xy *= mix(vec2(1.0), vec2(-1.0), step(vec2(1.0), mod(uv, 2.0)));
    }
#endif
    result.color += albedo.rgb * weight;
#if DEMI_PERFORMANCE_DEBUG && DEMI_PERFORMANCE_DEBUG_DISABLE_TERRAIN_NORMALS
    result.normal += receiverNormal * weight;
#else
    result.normal += terrainNormalFrame(receiverNormal, normalBasis, axis, localNormal,
                                         terrainNormalize(mapped, vec3(0.0, 0.0, 1.0))) * weight;
#endif
    if (detail.z > 0.5)
    {
        result.emissive += texture2DArrayLod(s_terrainEmissive, coordinate, mip).rgb * weight;
    }
    result.occlusion += ormHeight.r * weight;
    result.properties.xy += ormHeight.bg * weight;
    result.height += ormHeight.a * weight;
}

TerrainMappedSurface terrainMappedLayer(uint index, bool clockwiseFacing,
    vec3 localDx, vec3 localDy, TerrainNormalBasis normalBasis, vec3 receiverNormal,
    vec3 localPosition, vec3 localNormal, vec3 projectionWeights, float detailFade)
{
    vec4 tint = terrainPaletteTexel(index * 5u);
    vec4 parameters = terrainPaletteTexel(index * 5u + 1u);
    vec4 mapping = terrainPaletteTexel(index * 5u + 2u);
    vec4 strengths = terrainPaletteTexel(index * 5u + 3u);
    vec4 detail = terrainPaletteTexel(index * 5u + 4u);
    TerrainMappedSurface result;
    result.color = vec3(0.0);
    result.normal = vec3(0.0);
    result.emissive = vec3(0.0);
    result.properties = vec3(0.0, 0.0, parameters.z);
    result.occlusion = 0.0;
    result.height = 0.0;
    result.heightStrength = strengths.z;
#ifdef DEMI_TERRAIN_OPAQUE
    // Uniform palette culling is already enforced by the raster state.
    result.visible = 1.0;
#else
    result.visible = parameters.w > 0.5 && clockwiseFacing ? 0.0 : 1.0;
#endif
#if DEMI_PERFORMANCE_DEBUG && DEMI_PERFORMANCE_DEBUG_DISABLE_TERRAIN_MATERIALS
    // Diagnostic-only palette baseline: retain authored scalar properties and
    // categorical coverage, with no sampled surface maps or height bias.
    result.color = tint.rgb;
    result.normal = receiverNormal;
    result.properties = parameters.xyz;
    result.occlusion = 1.0;
    result.height = 0.5;
    result.heightStrength = 0.0;
    return result;
#else
#if DEMI_PERFORMANCE_DEBUG && DEMI_PERFORMANCE_DEBUG_DISABLE_TERRAIN_TRIPLANAR
    projectionWeights = vec3(0.0, 1.0, 0.0);
#else
    if (mapping.z < 0.5)
    {
        projectionWeights = vec3(0.0, 1.0, 0.0);
    }
#endif
    // Literal axes let the compiler specialize projection orientation and
    // remove dynamic-axis indexing while retaining all triplanar contributors.
    accumulateTerrainProjection(0, projectionWeights.x, mapping, strengths, detail,
        detailFade, localPosition, localNormal, localDx, localDy,
        normalBasis, receiverNormal, result);
    accumulateTerrainProjection(1, projectionWeights.y, mapping, strengths, detail,
        detailFade, localPosition, localNormal, localDx, localDy,
        normalBasis, receiverNormal, result);
    accumulateTerrainProjection(2, projectionWeights.z, mapping, strengths, detail,
        detailFade, localPosition, localNormal, localDx, localDy,
        normalBasis, receiverNormal, result);
    result.color *= tint.rgb;
#if !(DEMI_PERFORMANCE_DEBUG && DEMI_PERFORMANCE_DEBUG_DISABLE_TERRAIN_MACRO)
    if (strengths.w > 0.0)
    {
        result.color *= 1.0 - strengths.w * terrainMacroNoise(localPosition * detail.x);
    }
#endif
    return result;
#endif
}

vec4 terrainMappedSurface(uvec4 indices, vec4 weights, bool clockwiseFacing,
    vec3 localDx, vec3 localDy, vec3 worldDx, vec3 worldDy,
    vec3 receiverNormal, vec3 localPosition, vec3 localNormal, float viewDistance,
    out vec4 parameters, out vec3 lightingNormal, out float occlusion, out vec3 emissive)
{
    if (u_shadowConfig.x < -0.5 || u_debugMode.x > 7.5)
    {
#ifndef DEMI_TERRAIN_OPAQUE
        // Supported mapped terrain is opaque. Height factors stay positive,
        // so categorical culling of positive contributors alone determines
        // coverage; sampling shading maps cannot change the caster silhouette.
        float visibleWeight = 0.0;
        for (int slot = 0; slot < 4; ++slot)
        {
            if (weights[slot] > 0.0)
            {
                vec4 properties = terrainPaletteTexel(indices[slot] * 5u + 1u);
                if (!(properties.w > 0.5 && clockwiseFacing))
                {
                    visibleWeight += weights[slot];
                }
            }
        }
        if (visibleWeight <= 0.0)
        {
            discard;
        }
#endif
        parameters = vec4(0.0);
        lightingNormal = receiverNormal;
        occlusion = 1.0;
        emissive = vec3(0.0);
        return vec4(1.0);
    }
    TerrainNormalBasis normalBasis = terrainNormalBasis(
        receiverNormal, worldDx, worldDy, localDx, localDy);
    vec3 projectionWeights = abs(terrainNormalize(localNormal, vec3(0.0, 1.0, 0.0)));
    projectionWeights /= projectionWeights.x + projectionWeights.y + projectionWeights.z;
    float detailFade = 1.0 - smoothstep(u_terrainSurface.y * 0.5, u_terrainSurface.y, viewDistance);
    vec3 color = vec3(0.0);
    lightingNormal = vec3(0.0);
    parameters = vec4(0.0);
    occlusion = 0.0;
    emissive = vec3(0.0);
    for (int slot = 0; slot < 4; ++slot)
    {
        if (weights[slot] <= 0.0)
        {
            continue;
        }
        TerrainMappedSurface layer = terrainMappedLayer(indices[slot], clockwiseFacing,
            localDx, localDy, normalBasis, receiverNormal,
            localPosition, localNormal, projectionWeights, detailFade);
        // Height bias depends only on this contributor. Accumulate its weighted
        // channels now, then normalize once without retaining four surfaces.
        // Zero strength preserves the original interpolation footprint.
        weights[slot] *= layer.visible;
        weights[slot] *= terrainHeightBlendFactor(layer.height, layer.heightStrength);
        if (weights[slot] <= 0.0)
        {
            continue;
        }
        color += layer.color * weights[slot];
        lightingNormal += layer.normal * weights[slot];
        parameters.xyz += layer.properties * weights[slot];
        occlusion += layer.occlusion * weights[slot];
        emissive += layer.emissive * weights[slot];
    }
    float total = dot(weights, vec4(1.0));
#ifndef DEMI_TERRAIN_OPAQUE
    if (total <= 0.0)
    {
        discard;
    }
#endif
    parameters.xyz /= total;
    occlusion /= total;
    emissive /= total;
    lightingNormal = terrainNormalize(lightingNormal / total, receiverNormal);
    return vec4(color / total, 1.0);
}
