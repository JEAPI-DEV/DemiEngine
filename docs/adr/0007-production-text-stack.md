# ADR 0007: Production text stack

Status: accepted for Step 3 foundation

## Decision

Demi owns immutable text-layout values and uses `utf8proc` for UTF-8
validation, code-point categories, and extended grapheme boundaries on Linux
and Android. Font rasterization and GPU atlas lifetime remain behind
`FontAtlas2D`.

Complex-script shaping and bidi resolution will be supplied by a
`TextShaper` backend using HarfBuzz plus FriBidi (or an equivalent Unicode bidi
implementation). They are not embedded in renderers. Until that adapter is
connected, the layout result explicitly reports `shapingComplete = false` for
text that needs it. It must never silently claim byte-wise layout is correct.

## Alternatives considered

- ICU provides segmentation and bidi but has a substantially larger Android
  footprint than Demi needs for the retained UI foundation.
- Hand-written UTF-8, grapheme, bidi, or shaping algorithms were rejected: the
  edge cases and Unicode update burden are unsuitable for engine code.
- HarfBuzz alone does not provide bidi paragraph resolution or line breaking,
  so it is not a complete dependency choice by itself.

This boundary lets the shaping implementation change without changing HUD
documents, Lua APIs, layout tests, or render backends.
