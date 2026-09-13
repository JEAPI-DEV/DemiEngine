#include "CompoundTransitionProbe.h"

#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
void row(unsigned chunks, unsigned groupSize, unsigned repeat,
         const CompoundTransitionResult &r) {
  std::cout << chunks << ',' << groupSize << ',' << repeat << ',' << r.groups
            << ',' << r.peakBodies << ',' << r.initialShapeMs << ','
            << r.blastTotalMs << ',' << r.damageMs << ',' << r.splitMs << ','
            << r.replacementShapesMs << ',' << r.prepareBodiesMs << ','
            << r.commitMs << ',' << r.rollbackMs << ',' << r.physicsTotalMs
            << ',' << r.physicsMaxMs << ',' << r.cleanupMs << ',' << r.massError
            << ',' << r.linearMomentumError << ',' << r.angularMomentumError
            << '\n';
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--benchmark") {
      std::cout
          << "chunks,group_size,repeat,groups,peak_bodies,initial_shape_ms,"
             "blast_total_ms,damage_ms,split_ms,replacement_shapes_ms,prepare_"
             "bodies_ms,"
             "commit_ms,rollback_ms,physics_total_ms,physics_max_ms,cleanup_ms,"
             "mass_error,linear_momentum_error,angular_momentum_error\n";
      std::cout << std::fixed << std::setprecision(6);
      for (unsigned count : {16U, 64U, 256U})
        for (unsigned groupSize : {1U, 4U})
          for (unsigned repeat = 0; repeat < 12; ++repeat) {
            const auto result = runCompoundTransitionProbe(
                {.chunks = count, .groupSize = groupSize, .physicsSteps = 120});
            require(result.committed, "benchmark transition did not commit");
            if (repeat >= 2)
              row(count, groupSize, repeat - 2, result);
          }
      return 0;
    }
    if (argc != 1)
      throw std::runtime_error(
          "Usage: demi-blast-compound-probe [--benchmark]");
    for (unsigned iteration = 0; iteration < 20; ++iteration) {
      const auto split = runCompoundTransitionProbe({});
      require(split.committed && split.groups == 3 && split.peakBodies == 5,
              "connected chunks did not transition from one body to three "
              "compounds");
      const auto intact = runCompoundTransitionProbe({.groupSize = 6});
      require(!intact.committed && intact.groups == 1 && intact.peakBodies == 2,
              "undamaged assembly was needlessly replaced");
      const auto failed = runCompoundTransitionProbe({.maximumBodies = 4});
      require(!failed.committed && failed.peakBodies == 4,
              "capacity failure did not roll back staged bodies");
      const auto cancelled =
          runCompoundTransitionProbe({.cancelBeforeCommit = true});
      require(!cancelled.committed && cancelled.peakBodies == 5,
              "cancellation did not preserve the intact assembly");
    }
    std::cout << "Compound transition probe passed: 80 world lifetimes; "
                 "intact/grouped "
                 "bodies, mass/inertia/momentum, chunk queries, gravity, "
                 "cancellation, "
                 "capacity rollback, and cleanup.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
