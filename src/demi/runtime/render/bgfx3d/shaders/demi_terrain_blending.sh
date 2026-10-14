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

#endif
