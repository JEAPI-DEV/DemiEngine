// Dear ImGui's bgfx wrapper normally owns both stb implementations. DemiEngine
// already owns stb_truetype in FontAtlas2D, so the editor supplies only the
// rectangle packer required by ImGui's font atlas.
#define STB_RECT_PACK_IMPLEMENTATION
#include <stb/stb_rect_pack.h>
