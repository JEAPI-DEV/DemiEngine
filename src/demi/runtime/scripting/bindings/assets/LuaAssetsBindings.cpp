#include "demi/runtime/scripting/bindings/assets/LuaAssetsBindings.h"

#include "demi/runtime/assets/RuntimeAssetService.h"

#include <sol/sol.hpp>

#include <tuple>

namespace demi::runtime {
namespace {

std::string firstError(const Diagnostics &diagnostics) {
  return diagnostics.empty()
             ? std::string{}
             : diagnostics.front().code + ": " + diagnostics.front().message;
}

} // namespace

void LuaAssetsBindingModule::install(LuaScriptHost &host,
                                     lua_State *state) const {
  sol::state_view lua(state);
  sol::table assetsTable = lua.create_named_table("Assets");
  assetsTable.set_function("load", [&host](const std::string &uri) {
    Diagnostics diagnostics;
    RuntimeAssetService *assets = host.runtimeAssetService();
    const auto request =
        assets == nullptr ? 0 : assets->load(uri, &diagnostics);
    return std::tuple{request, firstError(diagnostics)};
  });
  assetsTable.set_function("progress", [state,
                                        &host](const std::uint64_t request) {
    sol::state_view view(state);
    sol::table result = view.create_table();
    RuntimeAssetService *assets = host.runtimeAssetService();
    const auto progress =
        assets == nullptr
            ? demi::assets::AssetGroupProgress{.stage = demi::assets::
                                                   AssetGroupStage::Failed,
                                               .error = "Runtime asset service "
                                                        "is unavailable."}
            : assets->progress(request);
    result["stage"] = demi::assets::assetGroupStageName(progress.stage);
    result["fraction"] = progress.fraction;
    result["completed_assets"] = progress.completedAssets;
    result["total_assets"] = progress.totalAssets;
    result["pending_bytes"] = progress.pendingBytes;
    result["decoded_bytes"] = progress.decodedBytes;
    result["resident_bytes"] = progress.residentBytes;
    result["error"] = progress.error;
    return result;
  });
  assetsTable.set_function("is_ready", [&host](const std::uint64_t request) {
    RuntimeAssetService *assets = host.runtimeAssetService();
    return assets != nullptr && assets->progress(request).stage ==
                                    demi::assets::AssetGroupStage::Ready;
  });
  assetsTable.set_function("cancel", [&host](const std::uint64_t request) {
    RuntimeAssetService *assets = host.runtimeAssetService();
    return assets != nullptr && assets->cancel(request);
  });
  assetsTable.set_function("unload", [&host](const std::string &uri) {
    Diagnostics diagnostics;
    RuntimeAssetService *assets = host.runtimeAssetService();
    const bool success =
        assets != nullptr && assets->unload(uri, &diagnostics);
    return std::tuple{success, firstError(diagnostics)};
  });
  assetsTable.set_function("reload", [&host](const std::string &assetId) {
    Diagnostics diagnostics;
    RuntimeAssetService *assets = host.runtimeAssetService();
    const bool success =
        assets != nullptr && assets->reload(assetId, &diagnostics);
    return std::tuple{success, firstError(diagnostics)};
  });
  assetsTable.set_function("memory_report", [state, &host] {
    sol::state_view view(state);
    sol::table result = view.create_table();
    RuntimeAssetService *assets = host.runtimeAssetService();
    const auto report = assets == nullptr ? demi::assets::AssetMemoryReport{}
                                          : assets->memoryReport();
    result["pending_bytes"] = report.pendingBytes;
    result["decoded_bytes"] = report.decodedBytes;
    result["resident_bytes"] = report.residentBytes;
    sol::table entries = view.create_table();
    int index = 1;
    for (const auto &entry : report.assets) {
      sol::table value = view.create_table();
      value["asset_id"] = entry.assetId;
      value["backend"] = entry.backend;
      value["resident_bytes"] = entry.residentBytes;
      sol::table owners = view.create_table();
      int ownerIndex = 1;
      for (const std::string &owner : entry.owners)
        owners[ownerIndex++] = owner;
      value["owners"] = owners;
      entries[index++] = value;
    }
    result["assets"] = entries;
    return result;
  });
}

} // namespace demi::runtime
