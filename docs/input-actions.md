# Input Actions

Gameplay code should depend on named project actions rather than physical key
names. Define actions under `input.actions` in `demi.project.json`:

```json
{
  "input": {
    "actions": {
      "move": [
        {"input": "a", "scale": -1},
        {"input": "left", "scale": -1},
        {"input": "d", "scale": 1},
        {"input": "right", "scale": 1}
      ],
      "jump": ["space", "w", "up"],
      "fire": ["mouse:left"]
    }
  }
}
```

Bindings may be strings or objects with `input` and `scale`. Action names and
physical inputs are case-insensitive. Multiple active scaled bindings are
summed and clamped to `[-1, 1]`.

Lua exposes:

- `Input.action_down(name)` for held actions.
- `Input.action_pressed(name)` for the current keyboard/controller press edge.
- `Input.action_value(name)` for scaled axes.

Raw key APIs remain available for compatibility and text/editor-like controls,
but reusable gameplay scripts should use actions.
