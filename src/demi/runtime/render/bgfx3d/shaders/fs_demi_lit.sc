$input v_color0, v_normal, v_texcoord0, v_worldPos

#include "bgfx_shader.sh"

SAMPLER2D(s_texColor, 0);
uniform vec4 u_lightDirection;
uniform vec4 u_lightColor;
uniform vec4 u_ambientColor;
uniform vec4 u_tint;
uniform vec4 u_pointPositionRange[4];
uniform vec4 u_pointColorIntensity[4];
uniform vec4 u_spotPositionRange[4];
uniform vec4 u_spotDirectionOuter[4];
uniform vec4 u_spotColorIntensity[4];
uniform vec4 u_spotInner[4];

void main()
{
    float normalLength = length(v_normal);
    vec3 normal = v_normal / max(normalLength, 0.0001);
    vec3 directionalVector = -u_lightDirection.xyz;
    vec3 directionalDirection = directionalVector /
                                max(length(directionalVector), 0.0001);
    float diffuse = max(dot(normal, directionalDirection), 0.0);
    vec3 lighting = u_ambientColor.rgb +
                    u_lightColor.rgb * diffuse * u_lightDirection.w;
    for (int ii = 0; ii < 4; ++ii)
    {
        if (u_pointPositionRange[ii].w > 0.0 &&
            u_pointColorIntensity[ii].w > 0.0)
        {
            vec3 toLight = u_pointPositionRange[ii].xyz - v_worldPos;
            float distanceToLight = length(toLight);
            vec3 lightDirection = toLight / max(distanceToLight, 0.0001);
            float attenuation = max(1.0 - distanceToLight /
                                    u_pointPositionRange[ii].w, 0.0);
            float pointDiffuse = max(dot(normal, lightDirection), 0.0);
            lighting += u_pointColorIntensity[ii].rgb *
                        u_pointColorIntensity[ii].w * pointDiffuse *
                        attenuation * attenuation;
        }

        if (u_spotPositionRange[ii].w > 0.0 &&
            u_spotColorIntensity[ii].w > 0.0)
        {
            vec3 toSpot = u_spotPositionRange[ii].xyz - v_worldPos;
            float distanceToSpot = length(toSpot);
            vec3 lightDirection = toSpot / max(distanceToSpot, 0.0001);
            vec3 spotVector = u_spotDirectionOuter[ii].xyz;
            vec3 spotDirection = spotVector / max(length(spotVector), 0.0001);
            float cone = dot(-lightDirection, spotDirection);
            float coneAmount = smoothstep(u_spotDirectionOuter[ii].w,
                                          u_spotInner[ii].x, cone);
            float spotAttenuation = max(1.0 - distanceToSpot /
                                        u_spotPositionRange[ii].w, 0.0);
            float spotDiffuse = max(dot(normal, lightDirection), 0.0);
            lighting += u_spotColorIntensity[ii].rgb *
                        u_spotColorIntensity[ii].w * spotDiffuse * coneAmount *
                        spotAttenuation * spotAttenuation;
        }
    }
    vec4 albedo = texture2D(s_texColor, v_texcoord0) * v_color0 * u_tint;
    gl_FragColor = vec4(albedo.rgb * lighting, albedo.a);
}
