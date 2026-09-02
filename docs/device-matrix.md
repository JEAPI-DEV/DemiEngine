# Qualified Device Matrix

`demi test android --project examples/minimal_2d_android` qualifies one
attached device at a time against the reference project. The run writes
`build/android/qualification/qualification.json` with device metadata, step
timing, screenshots, Lua end-to-end test results, runtime markers, and fatal
logcat checks. Record every qualified device here so API/GPU/vendor coverage
stays visible.

| Device  | Model | API | ABI      | GPU    | Last qualified | Result              |
|---------|-------|-----|----------|--------|----------------|---------------------|
| 33151FDH2005TS | Pixel 7 | 35 | arm64-v8a | mali (Mali-G715) | 2026-09-02 | pass (Lua test flow) |

## Qualifying a new device

1. Attach the device and confirm `adb devices -l` lists it in the `device`
   state. Use `--serial <id>` when more than one device is attached.
2. Run `demi test android --project examples/minimal_2d_android`
   (`--serial <id>` as needed). The gate builds a current debug APK,
   installs, launches, and either drives the menu through in-app Lua tests
   (`scripts/tests/mobile.lua`) or adb-resolved taps, then collects
   screenshots, runtime markers, and crash checks.
3. Confirm `qualification.json` reports `success: true` with
   `missing_runtime_markers: []` and `fatal_markers: []`, then append a row
   above with the serial, API level, ABI, GPU (`renderer` field), date, and
   result.

An emulator is not required for local release qualification; a CI device farm
may supplement this matrix later.
