# GIF Main Menu

Portrait HUD probe for animated GIF and SVG-backed `Icon2D` assets.

```bash
./build/linux-debug/demi validate examples/main_menu_gif/demi.project.json
./build/linux-debug/demi run --project examples/main_menu_gif/demi.project.json
```

`GifAnimation2D` plays automatically with the GIF's native per-frame delays. `Icon2D` rasterizes an SVG source at runtime; tint it through the usual HUD image color.
