#pragma once

namespace demi::runtime {

// Hydraulic-only accounting in discrete sample-height units, not physical
// mass. Thermal transport is excluded from these totals. Export leaves through
// open field edges. Suspended is aggregate unfinished bounded transport, not
// retained fluid state; it must not be mistaken for an unreported mass loss.
struct TerrainErosionTransport {
  double exportedSediment = 0;
  double suspendedSediment = 0;
  double erodedSediment = 0;
  double depositedSediment = 0;
  bool operator==(const TerrainErosionTransport &) const = default;
};

} // namespace demi::runtime
