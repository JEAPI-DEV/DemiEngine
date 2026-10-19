#ifndef DEMI_UNDERWATER
#define DEMI_UNDERWATER

SAMPLER2D(s_underwaterDepth, 1);
uniform vec4 u_underwaterOptics;
uniform vec4 u_underwaterColor;
uniform vec4 u_underwaterPlane;
uniform vec4 u_underwaterForward;
uniform vec4 u_underwaterRight;
uniform vec4 u_underwaterUp;
uniform vec4 u_underwaterBasisX;
uniform vec4 u_underwaterBasisY;
uniform vec4 u_underwaterBasisZ;

vec3 underwaterComposite(vec3 source, vec2 sceneUv, vec2 screenUv)
{
    if (u_underwaterOptics.x < 0.5)
    {
        return source;
    }
    vec2 ndc = vec2(screenUv.x * 2.0 - 1.0, 1.0 - screenUv.y * 2.0);
    vec3 lateral = u_underwaterRight.xyz * (ndc.x * u_underwaterRight.w) +
        u_underwaterUp.xyz * (ndc.y * u_underwaterUp.w);
    bool perspective = u_underwaterOptics.w > 0.5;
    vec3 ray = perspective ? normalize(u_underwaterForward.xyz + lateral) :
        u_underwaterForward.xyz;
    vec3 origin = perspective ? vec3(0.0) : lateral;
    float sceneDepth = texture2D(s_underwaterDepth, sceneUv).r * u_underwaterForward.w;
    float path = sceneDepth / max(dot(ray, u_underwaterForward.xyz), 0.0001);
    float planeDistance = u_underwaterPlane.w - dot(origin, u_underwaterPlane.xyz);
    float exiting = dot(ray, u_underwaterPlane.xyz);
    if (planeDistance <= 0.0)
    {
        return source;
    }
    if (exiting > 0.0001)
    {
        // Looking into air does not attenuate the whole sky-distance ray.
        path = min(path, planeDistance / exiting);
    }
    vec3 localRay = u_underwaterBasisX.xyz * ray.x +
        u_underwaterBasisY.xyz * ray.y + u_underwaterBasisZ.xyz * ray.z;
    float localPath = max(path, 0.0) * length(localRay);
    float opacity = (1.0 - exp(-localPath * u_underwaterOptics.y)) *
        u_underwaterOptics.z * u_underwaterColor.a;
    vec3 linearSource = mix(source / 12.92,
        pow((source + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), source));
    vec3 color = max(mix(linearSource, u_underwaterColor.rgb, clamp(opacity, 0.0, 1.0)), vec3(0.0));
    return mix(color * 12.92, 1.055 * pow(color, vec3(1.0 / 2.4)) - 0.055,
        step(vec3(0.0031308), color));
}

#endif
