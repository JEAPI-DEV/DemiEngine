#ifndef DEMI_MESH_WIND_SH
#define DEMI_MESH_WIND_SH

// Strength is a world-space displacement bound. Phase depends on the instance
// root, so a blade bends as one piece and different instances do not sway alike.
// All built-in static passes share this function, including shadow and depth.
uniform vec4 u_meshWind;

vec3 demiMeshWind(vec3 worldPosition, vec3 root, float localHeight)
{
    float heightWeight = clamp(localHeight / max(u_meshWind.z, 0.000001), 0.0, 1.0);
    float phase = dot(root.xz, vec2(0.173, 0.317))
                + u_meshWind.w * u_meshWind.y * 6.28318530718;
    float bend = u_meshWind.x * heightWeight * heightWeight * sin(phase);
    return worldPosition + vec3(0.8, 0.0, 0.6) * bend;
}

#endif
