# Chess

A complete local chess game in which the player controls White and the built-in chess engine controls Black.

- Click a white piece and then a highlighted destination square.
- Castle by clearing the path, selecting the king, and choosing `g1` (`O-O`)
  or `c1` (`O-O-O`). The castling card reports when either move is legal.
- Press `N` or use **New Game** to restart.
- Press `1`, `2`, or `3` to select the engine search depth.
- Pawn promotion currently defaults to a queen.

The example deliberately separates chess rules, computer search, HUD presentation, and game orchestration. `scripts/chess/rules.lua` has no engine API dependencies and is tested directly with `demi package test examples/chess`.

Roboto is imported as the regular `asset://chess/roboto` `Font2D` asset and
selected through the HUD's generic `font` property. No renderer path or
example-specific font registration is hardcoded.
