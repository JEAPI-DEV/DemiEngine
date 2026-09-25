#include "demi/runtime/scene/composition/PrefabResolver.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>

int main() {
  using namespace demi::runtime::composition;
  using Json = nlohmann::json;
  Json document{{"entities", Json::array()}};
  constexpr int count = 2000;
  for (int i = 0; i < count; ++i)
    document["entities"].push_back({{"id", "p_" + std::to_string(i)}, {"prefab", "prefab://crate"}});
  document["instances"] = Json::array({{{"id", "p_0/nested"}, {"prefab", "prefab://crate"}}});
  const PrefabOriginIndex index(document);
  const auto lookup = [&](const std::string &id) { return index.find(id); };
  const auto nested = lookup("p_0/nested/root");
  assert(nested && nested->instanceId == "p_0/nested" && nested->localEntityId == "root");
  assert(!lookup("p_0") && !lookup("p_10extra/root"));
  std::array<double, 3> timings;
  for (double &time : timings) {
    const auto start = std::chrono::steady_clock::now();
    const PrefabOriginIndex measuredIndex(document);
    for (int i = 0; i < count; ++i) {
      const std::string id = "p_" + std::to_string(i);
      const auto origin = measuredIndex.find(id + "/body");
      assert(origin && origin->instanceId == id && origin->localEntityId == "body");
    }
    time = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  }
  std::ranges::sort(timings);
  std::cout << "2000 prefab-origin queries, median of 3: " << timings[1] << " ms\n";
}
