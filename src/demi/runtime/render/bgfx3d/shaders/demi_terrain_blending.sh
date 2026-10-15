#ifndef DEMI_TERRAIN_BLENDING
#define DEMI_TERRAIN_BLENDING

// Canonical cell triangles are (a,c,b) and (b,c,d). A majority wins;
// three distinct materials keep the first vertex deterministically.
uint terrainTriangleMaterialIndex(uint a, uint b, uint c, uint d, vec2 fraction)
{
    if (fraction.x + fraction.y <= 1.0)
    {
        return c == b ? c : a;
    }
    return c == d ? c : b;
}

// Bias only existing material contributors. No height map uses strength zero,
// giving exactly one; a neutral height of 0.5 is also independent of strength.
float terrainHeightBlendFactor(float height, float strength)
{
    return max(0.001, 1.0 + strength * (height * 2.0 - 1.0));
}

#endif
