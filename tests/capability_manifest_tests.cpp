#include "demi/capabilities/CapabilityManifest.h"
#include "demi/diagnostics/Diagnostic.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool containsCode(const demi::Diagnostics &diagnostics,
                  const std::string &code) {
  for (const demi::Diagnostic &diagnostic : diagnostics) {
    if (diagnostic.code == code) {
      return true;
    }
  }
  return false;
}

} // namespace

int main(const int argc, const char **argv) {
  const std::vector<std::string> api{"Entity.create", "Input.down",
                                     "Entity.create"};
  const nlohmann::json baseline = demi::capabilities::buildManifest(api);
  if (baseline.value("format_version", 0) != 1 ||
      baseline["components"].empty() ||
      baseline["lua_api"] !=
          nlohmann::json::array({"Entity.create", "Input.down"})) {
    std::cerr << "Capability manifest was not deterministic or complete.\n";
    return 1;
  }
  if (!demi::capabilities::comparePublicApi(baseline, baseline).empty()) {
    std::cerr << "An unchanged public API was reported as changed.\n";
    return 1;
  }

  nlohmann::json removedApi = baseline;
  removedApi["lua_api"].erase(removedApi["lua_api"].begin());
  const demi::Diagnostics removedDiagnostics =
      demi::capabilities::comparePublicApi(baseline, removedApi);
  if (!demi::hasErrors(removedDiagnostics) ||
      !containsCode(removedDiagnostics, "CAPABILITY_LUA_API_REMOVED")) {
    std::cerr << "A removed Lua API was not reported as breaking.\n";
    return 1;
  }

  nlohmann::json addedApi = baseline;
  addedApi["lua_api"].push_back("Scene.reload");
  const demi::Diagnostics addedDiagnostics =
      demi::capabilities::comparePublicApi(baseline, addedApi);
  if (demi::hasErrors(addedDiagnostics) ||
      !containsCode(addedDiagnostics, "CAPABILITY_LUA_API_ADDED")) {
    std::cerr << "A compatible Lua API addition was classified incorrectly.\n";
    return 1;
  }

  nlohmann::json requiredField = baseline;
  requiredField["components"][0]["fields"].push_back(
      {{"name", "new_required_field"},
       {"type", "string"},
       {"required", true},
       {"replicated", false}});
  const demi::Diagnostics fieldDiagnostics =
      demi::capabilities::comparePublicApi(baseline, requiredField);
  if (!demi::hasErrors(fieldDiagnostics) ||
      !containsCode(fieldDiagnostics, "CAPABILITY_REQUIRED_FIELD_ADDED")) {
    std::cerr << "A newly required component field was not reported as "
                 "breaking.\n";
    return 1;
  }

  if (argc != 2) {
    std::cerr << "Expected the source root for reference-gate verification.\n";
    return 1;
  }
  const std::filesystem::path sourceRoot = argv[1];
  std::ifstream gatesInput(sourceRoot / "capabilities" /
                           "reference_gates.json");
  const nlohmann::json gates = nlohmann::json::parse(gatesInput);
  const demi::Diagnostics gateDiagnostics =
      demi::capabilities::verifyReferenceGates(sourceRoot, gates);
  if (demi::hasErrors(gateDiagnostics)) {
    std::cerr << "Checked reference gates did not verify.\n";
    return 1;
  }
  return 0;
}
