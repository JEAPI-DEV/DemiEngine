# Orbit UI qualification

Qualified 2026-10-09 on Linux, native SDL/bgfx Vulkan. The package and example are
`packages/sources/demi.ui.orbit` and `examples/orbit_ui`.

## Shared ownership

- UI prefab resolution uses local conventional paths and declared verified locked
  package content. Package-local nested prefabs must appear in manifest files;
  ambiguous installed identities fail. The editor palette consumes the same
  resolver and package source index.
- Lua `Hud.create` feeds a declarative subtree through the shared HUD parser and
  one mutation transaction. Duplicate children reject the entire insertion.
  Orbit factories return ordinary prefab definitions, not a separate Lua layout.
- Runtime layout aligns docked right/bottom/center controls using their final
  constrained dimensions. Explicit anchors retain origin semantics. The editor
  no longer bakes size-dependent offsets when applying docking, and undocking
  preserves visible placement. This intentionally corrects the development-era
  docking contract; maintained layout docs contain migration guidance.
- Native canvas rendering owns rounded fills/borders, padded input content/caret,
  slider/toggle geometry and aspect-preserving image fitting. SVG icons use the
  existing SVG importer and tintable Icon2D resource path.
- Pointer targeting follows rendered layer order, ancestor visibility and scroll
  clips. Pointer activation is independent of keyboard focusability.
- Compound UI policy remains in the package. Controllers own subscriptions and
  dispose them; dropdowns additionally own a temporary outside-click target.

## Evidence

Ten focused CTest checks passed: canvas2d, ui-canvas-renderer, ui, ui-step3,
lua-scripting, package-manager, editor-hud-document, editor-hud-flow-placement,
editor-hud-hierarchy and ui-orbit-package. Package unit tests: 4 passed.
Orbit validation checked 84 reachable files with no diagnostics. Minimal 3D
validation also passed. Metadata regeneration check and `git diff --check` passed.

The real runtime E2E suite passed visibly at 5120 x 2806 and headlessly. It covers
tabs, dropdown option selection with nonzero bounds, exclusive choices, Apply /
Reset, runtime button and whole-settings creation, duplicate-child rollback,
scene transitions, stepper, pagination, modal confirmation, notification dismissal
and icon-button activation. All six registered scenes load. Linux cook and the
same visible E2E suite from the cooked project passed.

Native full-resolution captures were inspected for title/rail alignment, input
padding, popup layering, modal centering, inventory counts and icon proportions.
Resized copies are maintained under `tools/package-store/public/images/orbit/`.
The native editor opened the installed icon gallery and showed its prefabs in UI
Palette. The editor also started with the normal native Wayland selection; the
user supplied a confirming editor screenshot. X11 was used for automated mouse
input and window screenshots because the available capture tools work there.
No engine platform-specific workaround was introduced. Windows/Android were not
qualified in this change; SVG rendering still uses the engine's existing optional
SVG capability.

## Published state

Orbit 1.0.0 has 39 prefabs/compositions and 32 original SVG icons, BSD-3-Clause.
Source SVGs can also be used outside the engine. Main, expanded-control and icon
galleries are examples; game persistence and inventory policy are not implemented
by the kit. Starter tabs/dropdowns/choices have three entries; pagination supports
arbitrary page counts via a three-button window.

Store release: `20261009-orbit-ui-eb8e938a`. Server Composer checks, README/security
checks, catalog replacement checks, 105 Twig template lint and production cache
warmup passed before the atomic symlink switch. Health, listing/detail pages,
old-name replacement banner, old release API, README pages and Orbit docs returned
success after deployment. Hosted Orbit archive hash matches the staging archive.
The example lock now points to https://demiengine.de.

The three duplicate cards were renamed package identities, not separate versions:
core -> events, traversal -> checkpoints, language_file -> localization. Browsing
hides the old identity only when the replacement is published; old URLs, versions
and archives remain accessible. Existing archive READMEs already matched source,
so deploying README rendering did not require overwriting immutable releases.
