# ADR 0007: Production text stack

Status: accepted and implemented

## Decision

Demi owns immutable text-layout values and uses `utf8proc` for UTF-8
validation, code-point categories, and extended grapheme boundaries on Linux
and Android. Font rasterization and GPU atlas lifetime remain behind
`FontAtlas2D`.

Complex-script shaping and bidi resolution are supplied by Demi's
`TextShaper` adapter using HarfBuzz 14.2.1 and SheenBidi 3.0.0. HarfBuzz owns
OpenType script shaping; SheenBidi owns Unicode paragraph and visual-run
resolution. They are not embedded in renderers. Font data, direction, locale,
and scale enter through engine-owned value contracts, so either dependency can
be replaced without changing HUD documents or renderer APIs.

HarfBuzz's optional subset, raster, vector, GPU, utility, FreeType, ICU, GLib,
Graphite, and Cairo integrations are disabled. SheenBidi uses its unity build.
Both libraries compile from the same pinned source revisions for Linux and
Android, avoiding host-package variation and keeping the Android footprint to
the required shaping and bidi code.

## Alternatives considered

- ICU provides segmentation and bidi but has a substantially larger Android
  footprint than Demi needs for the retained UI foundation.
- Hand-written UTF-8, grapheme, bidi, or shaping algorithms were rejected: the
  edge cases and Unicode update burden are unsuitable for engine code.
- HarfBuzz alone does not provide bidi paragraph resolution or line breaking,
  so it is not a complete dependency choice by itself.
- FriBidi was considered, but SheenBidi provides the smaller C-only boundary
  Demi needs, has first-class UTF-8 input, and uses the same permissive license
  family as the rest of this stack.

This boundary lets the shaping implementation change without changing HUD
documents, Lua APIs, layout tests, or render backends.
