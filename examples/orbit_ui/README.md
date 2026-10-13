# Orbit UI preview

Install the package with `demi package install --project examples/orbit_ui`, then
run `demi run --project examples/orbit_ui`. Maximize the window for your monitor;
`--window-size 1160x810` uses the gallery's authored aspect ratio.

The main gallery links to the expanded component gallery, which links to all 32 icons. Try tabs, the quality
dropdown, radio choices, stepper, pagination, tooltip and modal confirmation.
Additional registered scenes show a glacier palette, main menu and inventory.
Open the project in the editor to select those scenes and inspect prefab instances.

`demi test linux --project examples/orbit_ui` exercises interactive controls and
runtime creation. `DEMI_HEADLESS=1` runs the same logic without a visible preview;
use a visible run as well when changing layout or rendering.

See the [package README](../../packages/sources/demi.ui.orbit/README.md) for factory
and binding documentation. Subscriptions belong to the example's scene controller;
no example game state is stored in the engine.
