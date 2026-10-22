$input v_color0, v_texcoord0

#include "bgfx_shader.sh"

SAMPLER2DMS(s_opaqueDepthMsaa, 0);
uniform vec4 u_depthResolveSamples;

void main()
{
    // Average depth invents a surface at silhouette edges. Optical thickness
    // instead uses the nearest covered sample; untouched samples stay at far.
    float nearestDepth = 1.0;
    ivec2 coordinate = ivec2(gl_FragCoord.xy);
#if BGFX_SHADER_LANGUAGE_SPIRV
    // Query the allocated image, since a driver may clamp requested MSAA.
    uint imageWidth;
    uint imageHeight;
    uint imageSamples;
    s_opaqueDepthMsaa.m_texture.GetDimensions(imageWidth, imageHeight, imageSamples);
    int samples = int(imageSamples);
#else
    int samples = int(u_depthResolveSamples.x);
#endif
    for (int sampleIndex = 0; sampleIndex < samples; ++sampleIndex)
    {
        float depth = texelFetch(s_opaqueDepthMsaa, coordinate, sampleIndex).r;
        // Comparisons also reject NaN and infinity without backend-specific
        // isnan/isinf intrinsics. Every legitimate depth is normalized [0,1].
        if (depth >= 0.0 && depth <= 1.0)
        {
            nearestDepth = min(nearestDepth, depth);
        }
    }
    gl_FragColor = vec4(nearestDepth, 0.0, 0.0, 1.0);
}
