# libuv Android qualification blocker

Qualification date: 2026-10-21.

`demi-package-android-multiplayer-ffa-shooter` fails during native dependency
compilation with NDK 29.0.14206865 targeting `aarch64-none-linux-android23`.
This is an existing dependency/platform mismatch, unrelated to EnTT storage or
Transform3D change notifications.

`cmake/DemiAsyncDependencies.cmake` pins libuv 1.52.1. Its
`SUPPORTED_PLATFORMS.md` lists Android 7.0 and
`-DANDROID_PLATFORM=android-24` as the minimum Android configuration. The
engine's Android project defaults to API 23. NDK 29 declares both pthread
barriers and `getifaddrs`/`freeifaddrs` as introduced in API 24.

The observed failures are:

- `src/thread-common.c`: undeclared `pthread_barrier_init`,
  `pthread_barrier_wait`, and `pthread_barrier_destroy`.
- After an isolated barrier-selection experiment, `src/unix/tcp.c` and
  `src/unix/linux.c`: undeclared `getifaddrs` and `freeifaddrs`.

The second failure confirms that selecting libuv's existing barrier fallback
alone does not qualify Android API 23. The experiment is not part of the
maintained dependency configuration. No interface-enumeration stub, runtime
symbol shim, or Android minimum-SDK change is included.

Android packaging remains blocked pending an explicit dependency-support
choice. A successful Linux build does not establish Android compatibility.
Future qualification must rebuild the exact Android target from unmodified
pinned dependency sources and rerun the packaging test; device behavior remains
a separate physical-device check. Windows and other platform configuration are
unchanged.
