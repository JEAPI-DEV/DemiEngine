# libuv Android minimum API qualification

Qualification date: 2026-10-21.

`demi-package-android-multiplayer-ffa-shooter` fails during native dependency
compilation with NDK 29.0.14206865 targeting `aarch64-none-linux-android23`.
This is an existing dependency/platform mismatch, unrelated to EnTT storage or
Transform3D change notifications.

`cmake/DemiAsyncDependencies.cmake` pins libuv 1.52.1. Its
`SUPPORTED_PLATFORMS.md` lists Android 7.0 and
`-DANDROID_PLATFORM=android-24` as the minimum Android configuration. The
engine's direct Gradle build previously defaulted to API 23. NDK 29 declares both pthread
barriers and `getifaddrs`/`freeifaddrs` as introduced in API 24.

The observed failures are:

- `src/thread-common.c`: undeclared `pthread_barrier_init`,
  `pthread_barrier_wait`, and `pthread_barrier_destroy`.
- After an isolated barrier-selection experiment, `src/unix/tcp.c` and
  `src/unix/linux.c`: undeclared `getifaddrs` and `freeifaddrs`.

The second failure confirmed that selecting libuv's existing barrier fallback
alone could not qualify Android API 23. The experiment is not part of the
maintained dependency configuration.

## Adopted platform requirement

Android minimum API is now 24 (Android 7.0), matching the pinned dependency.
Gradle, the project settings parser, schema, and editor defaults agree on 24;
explicit settings accept 24..36. Previously, native settings defaulted to 26
while direct Gradle builds used 23. Explicit higher minima in existing projects
remain unchanged. Compile SDK and target SDK remain 36.

There is no barrier patch, interface-enumeration stub, or runtime symbol shim.
The API-24 packaging test must compile unmodified pinned dependency sources.
Physical-device execution remains separate from packaging qualification.

## API-24 result

`demi-package-android-multiplayer-ffa-shooter` passed on 2026-10-21 after a fresh
ARM64 Debug configuration with NDK 29.0.14206865 and unmodified pinned libuv
sources. The APK build report records `min_sdk: 24`, `target_sdk: 36`.
The clean configure/download/build/package run took 655.58 seconds; this is
build duration, not runtime performance. Project settings tests passed,
including the 23/24 and 36/37 boundaries and omitted-field defaults. The minimal
2D Android example also validated without diagnostics. No physical-device run
was performed for this change.

Evidence: `/tmp/demi-api24-tests.log`; generated package report under
`examples/multiplayer_ffa_shooter/build/android/`.
