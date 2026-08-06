# Animation and Audio

Phase 7 keeps game-facing presentation policy independent from third-party
libraries. Animation evaluation produces engine-owned playback state that the
2D and 3D render adapters consume. Audio buses, scheduling, fades, snapshots,
concurrency, and lifecycle policy live in `AudioSystem`; miniaudio only creates
voices and applies the resulting parameters.

## Animation

`AnimationStateMachine` supports transition `blend_duration`, global playback
`speed`, normalized time, named events, 1D/2D blend spaces, weighted layers,
bone masks, additive-layer metadata, and explicit root-motion policy.

```json
{
  "initial_state": "locomotion",
  "speed": 1.0,
  "root_motion": false,
  "pause_policy": "pause",
  "states": {
    "idle": { "model_clip_name": "Idle", "duration": 1.0 },
    "walk": { "model_clip_name": "Walk", "duration": 0.8 },
    "run": {
      "model_clip_name": "Run",
      "duration": 0.55,
      "events": [{ "time": 0.18, "name": "footstep" }]
    },
    "attack": { "model_clip_name": "Attack", "duration": 0.4, "loop": false },
    "locomotion": { "model_clip_name": "Idle", "duration": 1.0 }
  },
  "blend_spaces": {
    "locomotion": {
      "parameter_x": "speed",
      "points": [
        { "state": "idle", "x": 0.0 },
        { "state": "walk", "x": 0.5 },
        { "state": "run", "x": 1.0 }
      ]
    }
  },
  "layers": {
    "upper_body": {
      "state": "attack",
      "weight": 0.0,
      "additive": false,
      "mask": ["Spine", "Arm.L", "Arm.R"]
    }
  },
  "transitions": {
    "attack_done": {
      "from": "attack",
      "to": "locomotion",
      "condition": "finished",
      "blend_duration": 0.1
    }
  }
}
```

Lua can set parameters, speed, layer weights, and root-motion opt-in, and can
inspect normalized time and the current transition:

```lua
Animation.set_number("player", "speed", move_amount)
Animation.set_layer_weight("player", "upper_body", aiming and 1 or 0)
local transition = Animation.transition("player")

local footstep = Events.subscribe("animation_event", function(event)
  if event.name == "footstep" then
    Audio.play("asset://audio/footstep", {
      bus = "sfx",
      spatial = "3d",
      x = player_x,
      y = player_y,
      z = player_z,
      concurrency_group = "footsteps",
      max_voices = 4,
      voice_stealing = "oldest",
    })
  end
end)
```

Root motion is disabled unless scene data or
`Animation.set_root_motion(entity, true)` explicitly enables it. State
`root_motion_track` data contains evenly spaced local-space positions over the
clip duration. The animation system extracts its delta, including across loop
boundaries, and applies it independently of the renderer. For older data,
`root_motion_per_second` remains a fallback. `pause_policy` is either `pause`
or `continue`.

For regular sprite sheets, `SpriteAnimator2D.atlas` can generate one clip per
row:

```json
{
  "frame_size": [32, 32],
  "atlas": {
    "columns": 6,
    "rows": 3,
    "row_names": ["idle", "walk", "attack"],
    "fps": 12,
    "loop": true
  },
  "clip": "idle"
}
```

Explicit `clips` may override generated row clips to add events or nonuniform
frame ranges. Model import settings may declare stable clip names and skeleton
IDs; validation rejects missing names, duplicate names, and mixed skeletons.
Scene validation rejects missing state references in initial states,
transitions, blend spaces, and layers.

## Audio

The built-in bus tree is:

```text
master
├── music
├── sfx
├── voice
└── ui
```

Each bus has volume, mute, and pause state. Custom buses may be routed below an
existing bus in C++. Lua provides normal runtime mixing:

```lua
Audio.set_bus_volume("music", 0.8)
Audio.set_bus_muted("voice", false)
Audio.define_snapshot("gameplay", { music = 1.0, sfx = 1.0, voice = 1.0 })
Audio.define_snapshot("dialogue", { music = 0.35, sfx = 0.5, voice = 1.0 })
Audio.transition_snapshot("dialogue", 0.25)
```

`Audio.play` accepts loop/streaming, gain, pan, pitch, 2D/3D spatialization,
attenuation, Doppler, delayed start, fade-in, concurrency, voice stealing, and
pause policy. `Audio.crossfade` fades one handle out while a replacement fades
in. Mark long music and ambience with `"streaming": true` in the audio asset
settings so the backend does not preload decoded audio.

`AudioSource` exposes the same durable configuration in scene data. The audio
scene system updates listener/source positions independently from the backend.
Android suspension stops the device and resumes it without discarding logical
voices. `pause_with_game` determines whether a voice follows `Time.paused`.
