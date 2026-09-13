#pragma once

#include <cstdint>

// CPU-only experimental boundary. No Jolt or Blast types leave the fixture.
struct CompoundTransitionOptions {
  std::uint32_t chunks = 6;
  std::uint32_t groupSize = 2;
  std::uint32_t maximumBodies = 0;
  bool cancelBeforeCommit = false;
  unsigned physicsSteps = 240;
};

struct CompoundTransitionResult {
  bool committed = false;
  std::uint32_t groups = 0;
  std::uint32_t peakBodies = 0;
  double initialShapeMs = 0;
  double blastTotalMs = 0;
  double damageMs = 0;
  double splitMs = 0;
  double replacementShapesMs = 0;
  double prepareBodiesMs = 0;
  double commitMs = 0;
  double rollbackMs = 0;
  double physicsTotalMs = 0;
  double physicsMaxMs = 0;
  double cleanupMs = 0;
  float massError = 0;
  float linearMomentumError = 0;
  float angularMomentumError = 0;
};

// Throws on invariant failure; capacity rejection and explicit cancellation
// return committed=false after verifying the original body is still intact.
CompoundTransitionResult
runCompoundTransitionProbe(const CompoundTransitionOptions &options);
