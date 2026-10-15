#include "demi_terrain_blending.sh"

SAMPLER2D(s_terrainIds, 5);
SAMPLER2D(s_terrainPalette, 6);
// Sample dimensions and local world units per sample in X/Z, shared with VS.
uniform vec4 u_terrainField;
// Palette dimensions, then entry count encoded in low/high sixteen-bit words.
// Each uniform component remains exactly representable as a float.
uniform vec4 u_terrainPalette;

vec4 terrainPaletteTexel(uint index)
{
    uint width = uint(u_terrainPalette.x);
    vec2 coordinate = vec2(float(index % width), float(index / width));
    return texture2DLod(s_terrainPalette,
        (coordinate + vec2(0.5)) / u_terrainPalette.xy, 0.0);
}

uint terrainMaterialIndex(vec2 gridCoordinate)
{
    vec2 sampleUv = (gridCoordinate + vec2(0.5)) / u_terrainField.xy;
    uvec4 bytes = uvec4(texture2DLod(s_terrainIds, sampleUv, 0.0) * 255.0 + 0.5);
    uint index = bytes.x | (bytes.y << 8u) | (bytes.z << 16u) | (bytes.w << 24u);
    // Fetch exact IDs at lattice centres; filtering encoded bytes would invent
    // materials. Publication validates IDs, with a defensive boundary clamp.
    uint entries = uint(u_terrainPalette.z) | (uint(u_terrainPalette.w) << 16u);
    return min(index, entries - 1u);
}

#ifdef DEMI_TERRAIN_MAPPED
#include "demi_terrain_mapped.sh"
#endif

void accumulateTerrainMaterial(uint index, float weight, bool clockwiseFacing,
                               inout vec3 linearColor, inout vec3 properties,
                               inout float visibleWeight)
{
    if (weight <= 0.0)
    {
        return;
    }
    vec4 materialProperties = terrainPaletteTexel(index * 2u + 1u);
    // bgfx's default front face is clockwise. Culling remains categorical:
    // a culled material contributes neither colour nor shading properties.
    if (materialProperties.w > 0.5 && clockwiseFacing)
    {
        return;
    }
    vec4 color = terrainPaletteTexel(index * 2u);
    linearColor += decodeColor(color.rgb) * weight;
    properties += materialProperties.xyz * weight;
    visibleWeight += weight;
}

// Returns linear base RGB and opaque alpha. Main and shadow passes use the
// same material eligibility; no blend of cull flags can change their coverage.
vec4 terrainSurface(vec2 sampleUv, bool clockwiseFacing, float surfaceBlending,
                    vec3 localDx, vec3 localDy, vec3 worldDx, vec3 worldDy,
                    vec3 receiverNormal, vec3 localPosition, vec3 localNormal, float viewDistance,
                    out vec4 parameters, out vec3 lightingNormal,
                    out float occlusion, out vec3 emissive)
{
    vec2 gridPosition = clamp(sampleUv * u_terrainField.xy - vec2(0.5),
                              vec2(0.0), u_terrainField.xy - vec2(1.0));
    vec2 cell = min(floor(gridPosition), u_terrainField.xy - vec2(2.0));
    vec2 fraction = gridPosition - cell;
    uvec4 indices = uvec4(
        terrainMaterialIndex(cell),
        terrainMaterialIndex(cell + vec2(1.0, 0.0)),
        terrainMaterialIndex(cell + vec2(0.0, 1.0)),
        terrainMaterialIndex(cell + vec2(1.0, 1.0)));
    vec4 weights = vec4((1.0 - fraction.x) * (1.0 - fraction.y),
                        fraction.x * (1.0 - fraction.y),
                        (1.0 - fraction.x) * fraction.y,
                        fraction.x * fraction.y);

    if (surfaceBlending < 0.5)
    {
        uint selected = terrainTriangleMaterialIndex(
            indices.x, indices.y, indices.z, indices.w, fraction);
        indices = uvec4(selected);
        weights = vec4(1.0, 0.0, 0.0, 0.0);
    }

    // Merge duplicate palette IDs before fetching properties and colour.
    // Uniform cells need one palette pair instead of four.
    if (indices.y == indices.x)
    {
        weights.x += weights.y;
        weights.y = 0.0;
    }
    if (indices.z == indices.x)
    {
        weights.x += weights.z;
        weights.z = 0.0;
    }
    else if (indices.z == indices.y)
    {
        weights.y += weights.z;
        weights.z = 0.0;
    }
    if (indices.w == indices.x)
    {
        weights.x += weights.w;
        weights.w = 0.0;
    }
    else if (indices.w == indices.y)
    {
        weights.y += weights.w;
        weights.w = 0.0;
    }
    else if (indices.w == indices.z)
    {
        weights.z += weights.w;
        weights.w = 0.0;
    }

#ifdef DEMI_TERRAIN_MAPPED
    return terrainMappedSurface(indices, weights, clockwiseFacing, localDx, localDy,
                                worldDx, worldDy, receiverNormal, localPosition, localNormal,
                                viewDistance, parameters, lightingNormal, occlusion, emissive);
#else
    lightingNormal = receiverNormal;
    occlusion = 1.0;
    emissive = vec3(0.0);
    vec3 linearColor = vec3(0.0);
    vec3 properties = vec3(0.0);
    float visibleWeight = 0.0;
    accumulateTerrainMaterial(indices.x, weights.x, clockwiseFacing,
                              linearColor, properties, visibleWeight);
    accumulateTerrainMaterial(indices.y, weights.y, clockwiseFacing,
                              linearColor, properties, visibleWeight);
    accumulateTerrainMaterial(indices.z, weights.z, clockwiseFacing,
                              linearColor, properties, visibleWeight);
    accumulateTerrainMaterial(indices.w, weights.w, clockwiseFacing,
                              linearColor, properties, visibleWeight);
    if (visibleWeight <= 0.0)
    {
        discard;
    }
    parameters = vec4(properties / visibleWeight, 0.0);
    return vec4(linearColor / visibleWeight, 1.0);
#endif
}
