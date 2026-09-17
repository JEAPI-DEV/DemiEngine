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
local Animation = require("demi.animation")
local Events = require("demi.events")
local Audio = require("demi.audio")

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

Procedural rigs can solve a two-segment chain in either 2D or 3D. The target is
clamped when it lies outside the chain's reach, and the pole selects the bend
side or plane. This is suitable for planted feet, aiming arms, tentacles, and
mechanical linkages; terrain contact remains a gameplay decision made with the
normal physics queries.

```lua
local Animation = require("demi.animation")

local leg = Animation.solve_two_bone_3d({
  root = hip,
  target = foot_contact,
  pole = knee_hint,
  upper_length = 0.72,
  lower_length = 0.78,
})
Animation.set_bone_segment("creature", "front_left_upper", {
  start = hip,
  tail = leg.joint,
  pole = knee_hint,
})
```

`examples/procedural_spider_3d` combines this solver with 3D raycasts,
alternating planted-foot groups, generated steps and rocks, and a floor-to-wall
transition. Its Blender-authored skinned model contains named upper/lower leg
bones; runtime segment overrides deform that imported mesh without exposing
renderer handles or inverse-bind matrices to Lua. `Vector2`, `Vector3`, and
`Mathf.smoothstep` provide shared vector/interpolation math to gameplay code.

Root motion is disabled unless scene data or
`Animation.set_root_motion(entity, true)` explicitly enables it. State
`root_motion_track` data contains evenly spaced local-space positions over the
clip duration. The animation system extracts its delta, including across loop
boundaries, and applies it independently of the renderer. `pause_policy` is
either `pause` or `continue`.

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

### Animation performance evidence

`AnimationPlayer3D` supports opt-in temporal LOD:

```json
{ "clip_name": "Walk_Loop", "visual_update_rate": 30, "visual_update_distance": 30 }
```

This refreshes distant visual poses at up to 30 Hz beyond 30 world units; nearer
poses stay full rate. The default rate is zero (unrestricted). Distant poses are
held between samples, not interpolated, so this is an explicit visual-quality
tradeoff. Root transforms still render every frame. Gameplay clocks, events,
root motion, input and fixed-step collision are never throttled by these fields.
Stable entity phases distribute refreshes across frames. Clip/source changes,
rewinds and near-camera requests bypass the cadence; active blends, layers and
procedural overrides stay full rate. The shared Inspector/schema expose both
fields; the internal presentation clock is never serialized.

Animated meshes can also use existing `MeshRenderer.medium_lod_model` and
`low_lod_model` references. Candidates must be loaded and contain the same named
clip with matching duration; missing/incompatible clips, active blends/layers
and procedural overrides keep the high model. Author compatible origin, scale,
poses and bounds in the LOD assets. Colliders and authored model references do
not change. This is selection of authored models, not automatic decimation.

The crowd scene in `examples/animation_3d` provides matched live/frozen visual
animation workloads. See its README for the visible benchmark command. Runtime
profiling exposes `Renderer3D.animation_rebuild` (inclusive), `Renderer3D.skin_cpu`
(pose evaluation and vertex skinning), and `Renderer3D.skin_upload_cpu` (normal
reconstruction, vertex packing and upload submission, not GPU transfer timing).
Animated pose evaluation and vertex/normal preparation run in bounded batches on
the existing engine worker pool. GPU uploads remain on the render thread in
scene order. `Renderer3D.animation_prepare_wall` measures elapsed preparation time
including dispatch, joins and uploads. The other animation scope totals sum
per-character task durations, which can overlap; they are not frame latency.
`Renderer3D.mesh_vertices_cpu` and `Renderer3D.mesh_buffer_update_cpu` separate
vertex preparation from upload submission. Session percentiles describe individual
calls, not whole-frame animation totals. Batch gauges expose peak staged vertex
and mesh counts per camera and the available worker count.
Frozen cached poses do not emit rebuild scopes. UVs, packed vertex colors and
topology are reused from the model-owned cache and invalidated by asset reload;
positions and normals still update per animated pose on the CPU fallback.

Vulkan now automatically uses GPU skinning for supported `AnimationPlayer3D`
models: multiple skins and animated rigid parts, at most 128 referenced matrix
entries combined, valid weights and authored vertex normals,
with the built-in material and no procedural override or active layer/blend.
The original model buffers remain resident; only bone matrices are updated.
Referenced `(skin, joint)` pairs stay distinct so each skin keeps its own inverse
binds. Rigid vertices use their owner node's pose; unweighted skinned vertices
use identity before the common import transform, matching CPU behavior. Unused
joints do not consume GPU palette entries.
`GltfSkinnedModel3D::samplePose`/`bindPose` provide the shared engine-owned pose,
and `posePositions` is the CPU reference. Gameplay timing, root motion and
animation events remain CPU-owned and are not reduced in frequency.

Other models/materials and OpenGL/OpenGLES keep the CPU path. Set
`DEMI_GPU_SKINNING=0` before launch to force the CPU reference for diagnosis.
`Renderer3D.gpu_skinned_meshes` and `cpu_skinned_meshes` identify which path was
used; `skin_palette_cpu` measures CPU palette evaluation. GPU mode produces no
per-pose CPU vertex-rebuild/upload scopes. `animation_rebuild` counts a changed
pose on either path, not necessarily rebuilt vertices.

GPU lighting transforms the model's authored normals with the inverse-transpose
of the blended skin/import/model transform. CPU fallback still reconstructs
geometric normals. Shading can therefore differ; GPU skinning does not claim
pixel-identical lighting to the older reconstruction heuristic.

See [parallel character preparation](3d-parallel-character-preparation.md) for
the matched desktop measurements and remaining qualification limits.
See [GPU skinning](3d-gpu-skinning.md) for the subsequent GPU results and limits.

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
local Audio = require("demi.audio")

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
