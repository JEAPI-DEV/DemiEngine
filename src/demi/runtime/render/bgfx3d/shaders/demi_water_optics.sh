#ifndef DEMI_WATER_OPTICS
#define DEMI_WATER_OPTICS

float waterDepthContribution(float opticalPath, float absorptionDistance)
{
    return exp(-max(opticalPath, 0.0) / max(absorptionDistance, 0.000001));
}

float waterFresnel(float cosine)
{
    // Air/water dielectric interface, refractive index 1.333.
    float f0 = 0.0203732;
    float grazing = 1.0 - clamp(cosine, 0.0, 1.0);
    float squared = grazing * grazing;
    return f0 + (1.0 - f0) * squared * squared * grazing;
}

float waterRayPath(float sceneDepth, float surfaceDepth, float rayForwardCosine)
{
    return max(sceneDepth - surfaceDepth, 0.0) / max(rayForwardCosine, 0.0001);
}

float waterSurfaceOpacity(float shallowContribution, float shallowAlpha, float deepAlpha, bool below)
{
    // Below-water views exit into air. The underwater presentation pass owns
    // absorption along the camera-to-surface segment.
    return below ? 0.0 : clamp(mix(deepAlpha, shallowAlpha, shallowContribution), 0.0, 1.0);
}

float waterShoreFoam(float depth, float width)
{
    return width > 0.0 ? 1.0 - smoothstep(0.0, width, depth) : 0.0;
}

#endif
