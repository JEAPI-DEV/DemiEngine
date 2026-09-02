# Shipping DemiEngine Games

Project-owned release metadata belongs in the `build` object of
`demi.project.json`. The editor Build window edits the same data used by the
CLI and Gradle packager.

## Platform capability checks

`demi validate --platform linux|linux_server|android` cross-checks reachable
project content against what the target runtime supports, and every
`demi build` operation repeats the check before packaging. The Android
runtime is compiled without FFmpeg media and without librsvg; networking and
the graphics runtime are always present. The checks report stable errors for:

- `PROJECT_BUILD_FEATURE_MEDIA_UNSUPPORTED`: the project reaches video
  content (`VideoPlayer` components, `Video` Lua calls, or `VideoClip`
  assets referenced from scenes, HUDs, prefabs, resident project assets, or
  Lua) but the target runtime lacks FFmpeg media support.
- `PROJECT_BUILD_FEATURE_NETWORK_UNSUPPORTED`: the project reaches networking
  (`network_contract`, `NetworkContract` assets, `Network`/`NetworkSession`
  Lua calls) but the target runtime lacks ENet support.
- `PROJECT_BUILD_FEATURE_SVG_UNSUPPORTED`: the project references SVG
  textures/icons (`SvgTexture2D`/`Icon2D` assets) from runtime content, but
  the target runtime cannot decode SVG at run time. Rasterize the source or
  use a runtime with librsvg support. Branding-only SVG `icon`/`splash`
  sources never trigger this error because packaging rasterizes or installs
  them outside the engine runtime.
- `PROJECT_BUILD_PERMISSION_NETWORK_MISSING`: an authored Android
  `build.android.permissions` list omits `android.permission.INTERNET` while
  the project uses networking. Projects without an authored `build` block
  keep the Gradle default permissions, which include `android.permission.INTERNET`.

`demi doctor --platform <platform>` reports the same capability errors and
adds `DOCTOR_OPTIONAL_FEATURE_MISSING` warnings when the configured runtime
lacks an optional feature the project does not use.

## Android artifacts

Build a directly installable debug APK:

```sh
demi build apk --project path/to/demi.project.json
```

Release APKs and App Bundles require these environment variables:

- `DEMI_ANDROID_KEYSTORE`: path to the release keystore;
- `DEMI_ANDROID_KEYSTORE_PASSWORD`: keystore password;
- `DEMI_ANDROID_KEY_ALIAS`: signing key alias;
- `DEMI_ANDROID_KEY_PASSWORD`: signing key password.

Build a signed release APK or AAB:

```sh
demi build apk --configuration release --project path/to/demi.project.json
demi build aab --project path/to/demi.project.json
```

Signing values are read from the environment by Gradle. They are not passed as
command-line properties, copied into cooked content, written to reports, or
shown by the editor.

Artifacts are published to `<project>/build/android/`. Each artifact has a
neighboring `*.build-report.json` containing its hash, engine version,
project/cook hashes, application/version metadata, SDK policy, ABIs,
permissions, and graphics backends.

Debug APKs are signed by the Android SDK debug configuration. Release APKs and
AABs use the external release keystore. AAB files are intended for app-store
distribution and cannot be installed directly on a device.

## Branding

Android packaging accepts PNG, JPEG, WebP, and SVG source assets for `icon` and
`splash`. SVG sources are rasterized in generated build staging using
`rsvg-convert`; source asset files are never modified. Branding rasterization
is host-side only: SVG textures referenced from scenes or HUDs still require a
runtime with librsvg support at run time.

## Build progress

The editor reads Gradle's real task graph and displays the current task plus
completed and total task counts. Builds have no duration limit and remain
explicitly cancellable.

## Linux bundles

```sh
demi build linux --project path/to/demi.project.json
```

Linux bundles contain the configured executable and launcher name, cooked
project content, a freedesktop desktop entry, optional application icon,
third-party attribution notices, and `build-report.json`. The report records
the engine version, project/cook/runtime hashes, and the shared-library policy.

Game saves and writable data use `$XDG_DATA_HOME/<game>/`; caches use
`$XDG_CACHE_HOME/<game>/`. When those variables are absent, the runtime follows
the standard `$HOME/.local/share` and `$HOME/.cache` fallbacks. Packaged games
never need to write beside their executable or cooked project.
