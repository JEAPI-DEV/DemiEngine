# Repository documentation

The [website documentation](https://demiengine.de/docs) is the maintained
source for game authoring, public feature behavior, CLI use, and API guidance.
Its page bodies live in `tools/package-store/templates/docs/content/`; new pages
are registered in `tools/package-store/src/Docs.php`.

This directory keeps material that has a different job:

- [Architecture](architecture.md), [renderer migration](bgfx-migration.md),
  [capability gates](capability-gates.md), and the
  [text stack ADR](adr/0007-production-text-stack.md) record internal decisions
  and engineering boundaries.
- [Milestone 1 baseline](3d-milestone-1-baseline.md),
  [Milestone 2 qualification](3d-milestone-2-qualification.md), and the other
  `3d-*-scaling.md`, `3d-*-qualification.md`, and timing/optimization reports
  record scoped hardware, workloads, measurements, and limits. They are evidence,
  not general performance guarantees.
- [Compatibility policy](compatibility.md), [shipping audit](shipping.md), and
  [device matrix](device-matrix.md) retain repository-specific rules and
  qualification detail.

Other topic guides remain here while their unique content and incoming links
are checked against website pages. For a public feature change, update the
website page first. Keep an internal guide only when it records distinct design,
implementation, or qualification information. Do not remove a topic guide just
because a similarly named website page exists.
