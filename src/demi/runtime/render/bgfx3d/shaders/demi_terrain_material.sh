SAMPLER2D(s_terrainIds, 5);
SAMPLER2D(s_terrainPalette, 6);
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

vec4 terrainSurface(vec2 sampleUv, out vec4 parameters)
{
    uvec4 bytes = uvec4(texture2DLod(s_terrainIds, sampleUv, 0.0) * 255.0 + 0.5);
    uint index = bytes.x | (bytes.y << 8u) | (bytes.z << 16u) | (bytes.w << 24u);
    // CPU publication validates the map; a defensive clamp never indexes
    // outside the palette when sampling its far floating-point boundary.
    uint entries = uint(u_terrainPalette.z) | (uint(u_terrainPalette.w) << 16u);
    index = min(index, entries - 1u);
    parameters = terrainPaletteTexel(index * 2u + 1u);
    return terrainPaletteTexel(index * 2u);
}
