$input v_color0, v_normal, v_texcoord0, v_worldPos, v_relativePos, v_terrainBlend, v_localPos, v_localNormal

#define DEMI_TERRAIN 1
#define DEMI_TERRAIN_MAPPED 1
#define DEMI_TERRAIN_OPAQUE 1
#define DEMI_DIRECTIONAL_ONLY 1
#include "demi_surface_lighting.sh"
