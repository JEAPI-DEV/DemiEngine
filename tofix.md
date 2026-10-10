# Deferred editor fixes

- Inspector fields shift vertically when the first edit adds the Unsaved status
  line. Keep a fixed header height or move this indicator into an existing header.
  Reproduced while editing the colony camera: after changing Position X, the
  remaining axis fields moved down, so the next click missed its input.

- In a nested entity prefab, the child Transform Inspector shows Parent = None
  when parenting comes from the authored children array (observed on depot/rack).
  Show the effective inherited parent, or clearly distinguish an omitted explicit
  parent field from no hierarchy parent. Do not materialize parent fields merely
  to make the Inspector display them.
