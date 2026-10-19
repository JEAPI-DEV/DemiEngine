#ifndef DEMI_TERRAIN_NORMAL_FRAME
#define DEMI_TERRAIN_NORMAL_FRAME

float terrainFrameExtent(vec3 value)
{
    return max(abs(value.x), max(abs(value.y), abs(value.z)));
}

vec3 terrainNormalize(vec3 value, vec3 fallback)
{
    float extent = terrainFrameExtent(value);
    if (!(extent > 0.0))
    {
        return fallback;
    }
    vec3 scaled = value / extent;
    return scaled * inversesqrt(dot(scaled, scaled));
}

struct TerrainNormalBasis
{
    vec3 gradientX;
    vec3 gradientY;
    vec3 gradientZ;
    vec3 squaredLengths;
    float extent;
};

TerrainNormalBasis terrainNormalBasis(vec3 normal, vec3 worldDx, vec3 worldDy,
                                       vec3 localDx, vec3 localDy)
{
    TerrainNormalBasis basis;
    basis.gradientX = vec3(0.0);
    basis.gradientY = vec3(0.0);
    basis.gradientZ = vec3(0.0);
    basis.squaredLengths = vec3(0.0);
    basis.extent = 0.0;
    float scaleX = terrainFrameExtent(worldDx);
    float scaleY = terrainFrameExtent(worldDy);
    if (!(scaleX > 0.0) || !(scaleY > 0.0))
    {
        return basis;
    }
    vec3 perpendicularY = cross(worldDy / scaleY, normal);
    vec3 perpendicularX = cross(normal, worldDx / scaleX);
    vec3 dx = localDx / scaleX;
    vec3 dy = localDy / scaleY;
    // Every projection is a signed pair of these scalar-field gradients.
    // Positive authored tiling cancels in cotangent-frame normalization.
    basis.gradientX = perpendicularY * dx.x + perpendicularX * dy.x;
    basis.gradientY = perpendicularY * dx.y + perpendicularX * dy.y;
    basis.gradientZ = perpendicularY * dx.z + perpendicularX * dy.z;
    basis.extent = max(terrainFrameExtent(basis.gradientX),
        max(terrainFrameExtent(basis.gradientY), terrainFrameExtent(basis.gradientZ)));
    if (!(basis.extent > 0.0))
    {
        return basis;
    }
    vec3 x = basis.gradientX / basis.extent;
    vec3 y = basis.gradientY / basis.extent;
    vec3 z = basis.gradientZ / basis.extent;
    basis.squaredLengths = vec3(dot(x, x), dot(y, y), dot(z, z));
    return basis;
}

vec3 terrainNormalFrame(vec3 normal, TerrainNormalBasis basis, int axis,
                        vec3 localNormal, vec3 mapped)
{
    vec3 tangent;
    vec3 bitangent;
    float lengthSquared;
    if (axis == 0)
    {
        tangent = basis.gradientZ * (localNormal.x < 0.0 ? 1.0 : -1.0);
        bitangent = basis.gradientY;
        lengthSquared = max(basis.squaredLengths.z, basis.squaredLengths.y);
    }
    else if (axis == 1)
    {
        tangent = basis.gradientX;
        bitangent = basis.gradientZ * (localNormal.y < 0.0 ? -1.0 : 1.0);
        lengthSquared = max(basis.squaredLengths.x, basis.squaredLengths.z);
    }
    else
    {
        tangent = basis.gradientX * (localNormal.z < 0.0 ? -1.0 : 1.0);
        bitangent = basis.gradientY;
        lengthSquared = max(basis.squaredLengths.x, basis.squaredLengths.y);
    }
    if (lengthSquared >= 1.0e-20 && basis.extent >= 1.0e-20 && basis.extent <= 1.0e20)
    {
        float scale = inversesqrt(lengthSquared) / basis.extent;
        tangent *= scale;
        bitangent *= scale;
    }
    else
    {
        // An unused third gradient can dwarf this pair. Preserve the original
        // pair-local scaling when the common scale would lose its precision.
        float extent = max(terrainFrameExtent(tangent), terrainFrameExtent(bitangent));
        if (!(extent > 0.0))
        {
            return normal;
        }
        tangent /= extent;
        bitangent /= extent;
        float scale = inversesqrt(max(dot(tangent, tangent), dot(bitangent, bitangent)));
        tangent *= scale;
        bitangent *= scale;
    }
    return terrainNormalize(tangent * mapped.x + bitangent * mapped.y + normal * mapped.z, normal);
}

#endif
