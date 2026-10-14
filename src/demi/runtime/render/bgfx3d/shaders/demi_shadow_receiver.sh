#ifndef DEMI_SHADOW_RECEIVER_PLANE
#define DEMI_SHADOW_RECEIVER_PLANE

// Screen derivatives may be tiny even for an ordinary, well-conditioned plane.
// Normalize each Jacobian row together with its depth derivative, so rejection
// depends on relative projected area rather than camera/atlas coordinate scale.
vec2 receiverPlaneDepthGradient(vec2 uvDx, vec2 uvDy, vec2 depthDerivative)
{
    float scaleX = max(abs(uvDx.x), abs(uvDx.y));
    float scaleY = max(abs(uvDy.x), abs(uvDy.y));
    if (!(scaleX > 0.0) || !(scaleY > 0.0))
    {
        return vec2(0.0);
    }
    vec2 rowX = uvDx / scaleX;
    vec2 rowY = uvDy / scaleY;
    vec2 depth = depthDerivative / vec2(scaleX, scaleY);
    float determinant = rowX.x * rowY.y - rowX.y * rowY.x;
    float rowArea = sqrt(dot(rowX, rowX) * dot(rowY, rowY));
    // Nearly parallel rows amplify derivative noise. Retain the safe zero
    // correction for degenerate or poorly conditioned projections.
    if (!(abs(determinant) > 0.00001 * rowArea))
    {
        return vec2(0.0);
    }
    return vec2(rowY.y * depth.x - rowX.y * depth.y,
                rowX.x * depth.y - rowY.x * depth.x) / determinant;
}

// World-space receiver offset bounded by the filter footprint. The cosine is
// the normal/light alignment; the maximum protects against roundoff above one.
float receiverNormalOffsetDistance(float worldTexel, float cosine, float filterRadius)
{
    return worldTexel * filterRadius * sqrt(max(0.0, 1.0 - cosine * cosine));
}

#endif
