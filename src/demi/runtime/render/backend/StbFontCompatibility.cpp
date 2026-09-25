// bgfx's editor bridge still creates its bundled icon/mono fonts through stb.
// Runtime text and the editor's primary font use our variation-aware FreeType path.
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
