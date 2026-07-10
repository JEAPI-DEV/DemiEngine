# Animated Main Menu

A data-driven HUD menu with an explorer walking from left to right behind the menu surface. It is the smallest reusable probe for `require("demi.gui_animation")`.

```bash
./build/linux-debug/demi validate examples/main_menu_animated/demi.project.json
./build/linux-debug/demi run --project examples/main_menu_animated/demi.project.json
```

The timeline advances a callback from `0.0` to `1.0`; this example uses it for a looping position tween and `asset://walk/warrior`, a single `ImageAnimation2D` asset whose `sources` list owns the eight-frame PNG sequence.
