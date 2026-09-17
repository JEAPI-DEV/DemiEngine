# Android Lifecycle and Permissions

Android platform behavior is exposed through `ApplicationServices`; Lua does
not call JNI or SDL directly.

## Permissions

Only permissions listed in `build.android.permissions` may be requested:

```lua
local Application = require("demi.application")

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
- SDL window creation/resizing preserves an explicit current Android orientation
  when no explicit SDL hint is supplied, rather than resetting authored landscape
  or gameplay orientation requests to automatic rotation.
- Android's back key is available as `key:back` and also emits a
  `back_requested` lifecycle event.

`Application.take_lifecycle_events()` returns ordered events for focus,
minimize/restore, suspend/resume, low memory, display changes, safe-area
changes, and back requests. Existing polling functions remain available.

Background networking is paused by the default suspension policy. A future
explicit background-service feature must declare and validate its Android
capabilities rather than silently bypassing this policy.

## Attached-device workflow

Run the current project on one connected Android phone and stream logs:

```sh
demi run android --project path/to/demi.project.json
```

When multiple devices are attached, select one explicitly with `--serial`.
Add `--watch` to incrementally cook and synchronize changed scene, HUD, Lua,
data, and asset files into the debuggable app sandbox. Native engine changes
still require a rebuild/reinstall.

Run the physical-device lifecycle gate with:

```sh
demi test android --project path/to/demi.project.json --serial DEVICE
```

The gate writes `build/android/qualification/qualification.json`, `launch.png`,
`resumed.png` (for Lua-test projects), and `logcat.txt` beneath the project. It scopes crash detection to processes
launched by that qualification session because Android can retain protected
crash-buffer entries from older installations.
Qualification also requires Java surface creation, graphics initialization,
frame progression and a compositor frame-rate request marker. The dedicated
`minimal_2d_android` probe additionally requires its 60 FPS, scene and save
markers; arbitrary projects are not required to implement that demo's menu.
Lua tests qualify only their named cases, not implied save/network coverage.
After Lua tests, the runner relaunches without the test marker and checks Home /
foreground resume: the process must be retained, its own logs must show surface
destruction/rebinding, and a resumed screenshot is captured. The test app is
stopped afterwards. Performance qualification remains a separate measurement.
Default APK names follow the build display name (or project name), matching
the native packager, rather than the project directory name.

## Optimized device profiling

`scripts/benchmark_android_crowd.py` creates isolated variants of the animation
example, validates/cooks/audits them through the native APK packager, then
repackages those cooked assets in the development `profile` Gradle variant.
This variant uses `RelWithDebInfo` (`-O2 -DNDEBUG`) native code with debug signing
and an inspectable app sandbox. It is not a signed shipping Release artifact.
Shipping Release signing and behavior remain unchanged.

```sh
python3 scripts/benchmark_android_crowd.py \
  --output build/android-crowd-check --counts 64 256 \
  --workloads animated mixed --seconds 30 --warmup 5
```

Select an authorized device with `--serial` when necessary. The runner uses its
own `dev.jeapi.demi.m2profile` app identity, runs one case at a time, retains APK
hashes, build logs, frame traces, screenshots and thermal-service snapshots, and
stops its app after each case. The screen must stay unlocked and untouched.
APK installation uses ADB's non-streaming path to avoid observed streamed-install
stalls; ordinary Android system services can remain running during captures.
Default expected pixels are 2400x1080; use `--width`/`--height` to declare the
device's actual landscape drawable, not to force a lower render resolution.
`--rig-layout split` is the default geometry-preserving multi-skin fixture;
`--visual-rate 0` preserves full-rate poses, while positive rates apply beyond
30 world units. Mixed workloads retain active dynamic collision bodies.

Only the profile variant recognizes a private `files/.demi_profile` marker.
It directs the existing runtime profiler into fixed private `profile.csv` and
`profile.frames.csv` paths. There are no exported intent controls or arbitrary
output paths. The marker is not recognized in ordinary Debug/Release builds.
The benchmark's Lua timer ends the run normally so the trace is finalized.
Capture validity (population, dimensions, GPU measurements, completion) and
performance-budget success must be assessed separately, including discarded
fixed time. Thermal snapshots are context, not a controlled thermal soak.
