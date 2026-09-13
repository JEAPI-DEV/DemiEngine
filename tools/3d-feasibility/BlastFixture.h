#pragma once
#include <cstdint>
#include <vector>

struct BlastFixtureOptions {
  std::uint32_t chunks = 3;
  std::uint32_t rowWidth = 0; // Zero makes a chain; otherwise a 2D grid.
  std::uint32_t damageBatch = UINT32_MAX;
  float damage = 2.0F;
};

struct BlastFixtureResult {
  std::vector<std::vector<std::uint32_t>> groups;
  std::uint32_t bonds = 0;
  std::uint32_t applyCalls = 0;
  double assetMs = 0;
  double applyTotalMs = 0;
  double applyMaxMs = 0;
  double splitMs = 0;
};

BlastFixtureResult runBlastFixture(const BlastFixtureOptions &options);

// Probe boundary: detached chunk IDs, never Blast pointers or raw memory blobs.
std::vector<std::vector<std::uint32_t>> splitBlastFixture(float damage);
