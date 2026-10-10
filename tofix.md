# Deferred editor fixes

- Inspector fields shift vertically when the first edit adds the Unsaved status
  line. Keep a fixed header height or move this indicator into an existing header.
  Reproduced while editing the colony camera: after changing Position X, the
  remaining axis fields moved down, so the next click missed its input.
