# Deferred editor fixes

- In a nested entity prefab, the child Transform Inspector shows Parent = None
  when parenting comes from the authored children array (observed on depot/rack).
  Show the effective inherited parent, or clearly distinguish an omitted explicit
  parent field from no hierarchy parent. Do not materialize parent fields merely
  to make the Inspector display them.
