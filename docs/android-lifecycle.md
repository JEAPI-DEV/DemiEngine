# Android Lifecycle and Permissions

Android platform behavior is exposed through `ApplicationServices`; Lua does
not call JNI or SDL directly.

## Permissions

Only permissions listed in `build.android.permissions` may be requested:

```lua
local requested, error = Application.request_permission(
  "android.permission.RECORD_AUDIO")
```

`Application.permission_state(permission)` returns `unknown`,
`not_requested`, `requesting`, `granted`, `denied`, or
`denied_permanently`. Results are asynchronous and can be consumed with
`Application.take_permission_events()`.

Callbacks use a weak, generation-scoped state owner. A response arriving after
project replacement or runtime destruction is ignored safely.

## Lifecycle policy

- Backgrounding suspends simulation, rendering, audio, and ordinary network
  updates without destroying scene, script, or resource ownership.
- Foregrounding resumes the retained world. Drawable-size changes recreate the
  bgfx back buffer through the graphics-device resize contract.
- Save writes are atomic and immediate; there is no pending in-memory save
  queue to flush during suspension.
- Low-memory events release runtime asset residency and increment the public
  low-memory generation.
- Android storage uses the application's private internal data and cache roots.
- IME visibility, clipboard, orientation, safe-area, and permission operations
  are owned by `ApplicationServices`.
- Android's back key is available as `key:back` and also emits a
  `back_requested` lifecycle event.

`Application.take_lifecycle_events()` returns ordered events for focus,
minimize/restore, suspend/resume, low memory, display changes, safe-area
changes, and back requests. Existing polling functions remain available.

Background networking is paused by the default suspension policy. A future
explicit background-service feature must declare and validate its Android
capabilities rather than silently bypassing this policy.
