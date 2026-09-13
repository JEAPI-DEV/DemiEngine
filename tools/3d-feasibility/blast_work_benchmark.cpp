#include "BlastFixture.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>

int main(int argc, char **argv) {
  try {
    const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke";
    if (argc > 1 && !smoke)
      throw std::runtime_error("usage: demi-blast-work-benchmark [--smoke]");
    std::cout << "chunks,bonds,shape,damage,batch,repeat,apply_calls,asset_ms,"
                 "apply_total_ms,apply_max_ms,split_ms,actors\n";
    std::cout << std::fixed << std::setprecision(6);
    const std::vector<std::uint32_t> counts =
        smoke ? std::vector<std::uint32_t>{64}
              : std::vector<std::uint32_t>{250, 500, 1000, 2000, 4096};
    for (auto count : counts)
      for (auto width : {0U, 32U})
        for (float damage : {0.25F, 2.0F})
          for (auto batch : {64U, UINT32_MAX})
            for (int repeat = -2; repeat < (smoke ? 1 : 10); ++repeat) {
              const auto result =
                  runBlastFixture({count, width, batch, damage});
              std::vector<std::uint32_t> ids;
              for (const auto &group : result.groups)
                ids.insert(ids.end(), group.begin(), group.end());
              std::ranges::sort(ids);
              if (ids.size() != count ||
                  result.groups.size() != (damage < 1 ? 1U : count))
                throw std::runtime_error("batching changed fracture topology");
              for (std::uint32_t i = 0; i < count; ++i)
                if (ids[i] != i)
                  throw std::runtime_error("lost or duplicated chunk identity");
              if (repeat < 0)
                continue; // Per-case warmup; no timing assertions in CI.
              std::cout << count << ',' << result.bonds << ','
                        << (width ? "grid" : "chain") << ',' << damage << ','
                        << batch << ',' << repeat << ',' << result.applyCalls
                        << ',' << result.assetMs << ',' << result.applyTotalMs
                        << ',' << result.applyMaxMs << ',' << result.splitMs
                        << ',' << result.groups.size() << '\n';
            }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
