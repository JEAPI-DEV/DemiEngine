#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/scene/model/ProjectData.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/scripting/LuaScriptHost.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

bool writeFile(const std::filesystem::path &path,
               const std::string_view contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << contents;
  return output.good();
}

demi::AssetRegistry registryFor(const std::filesystem::path &root,
                                std::string hash) {
  demi::AssetRegistry registry{.projectDirectory = root};
  registry.assets.push_back({
      .id = "asset://data/probe",
      .type = "DataAsset",
      .importer = "json_data",
      .importerVersion = 1,
      .sourceHash = std::move(hash),
      .settingsJson = R"({"content_type":"probe","tags":["test:lua"]})",
      .manifestPath = root / "assets/probe.asset.json",
      .sourcePath = root / "assets/probe.json",
      .sourcePaths = {root / "assets/probe.json"},
  });
  return registry;
}

} // namespace

int main() {
  using namespace demi::runtime;
  const auto root =
      std::filesystem::temp_directory_path() / "demi_lua_data_bindings_tests";
  std::error_code filesystemError;
  std::filesystem::remove_all(root, filesystemError);

  if (!writeFile(root / "assets/probe.json", R"({
        "format_version": 1,
        "id": "probe",
        "null_value": null,
        "boolean": true,
        "integer": 7,
        "number": 1.5,
        "numeric_key": {"1": "object"},
        "empty_object": {},
        "empty_array": [],
        "array": [1, false, null]
      })") ||
      !writeFile(root / "scripts/probe.lua", R"lua(
local Flags = require("demi.data.flags")
local Conditions = require("demi.data.conditions")
local Inventory = require("demi.data.inventory")
local Quests = require("demi.data.quests")
local Dialogue = require("demi.data.dialogue")

local Probe = {}
function Probe:on_start()
  local document, load_error = Data.load("asset://data/probe")
  local missing, missing_error = Data.load("asset://data/missing")
  local matches = Data.query({ content_type = "probe", tags = {"test:lua"} })
  if load_error == nil and missing == nil and missing_error.code == "DATA_ASSET_NOT_FOUND"
      and document.boolean and document.integer == 7 and document.number == 1.5
      and document.array[1] == 1 and document.array[2] == false
      and Data.is_null(document.array[3]) and Data.is_null(document.null_value)
      and Data.kind(document.numeric_key) == "object"
      and Data.kind(document.empty_object) == "object"
      and Data.kind(document.empty_array) == "array"
      and #matches == 1 and Data.revision("asset://data/probe") == 1 then
    document.integer = 99
    if Data.load("asset://data/probe").integer == 7 then
      Save.set_string("data", "contract", "passed")
    end
  end

  local state = {}
  Flags.set(state, "met_mira", true)
  Inventory.add(state, "asset://items/sword", 2)
  local removed = Inventory.remove(state, "asset://items/sword", 1)
  local quest = { id = "quest:first", objectives = {{ id = "find", target = 1 }} }
  Quests.start(state, quest)
  local quest_events = Quests.progress(state, quest, "find")
  local dialogue = {
    id = "dialogue:intro", start = "hello",
    nodes = {
      { id = "hello", text = "Hello", choices = {
        { id = "continue", next = "done", condition = { id = "met_mira" } }
      }},
      { id = "done", text = "Done" },
    },
  }
  local session = Dialogue.start(dialogue)
  local node = Dialogue.current(dialogue, session, state)
  local choice = Dialogue.choose(dialogue, session, "continue", state)
  local unavailable, inventory_error = Inventory.remove(state, "asset://items/sword", 2)
  local bad_dialogue = {
    id = "dialogue:bad", start = "start",
    nodes = {{ id = "start", text = "Broken", choices = {{ id = "bad", next = "missing" }} }},
  }
  local bad_session = Dialogue.start(bad_dialogue)
  local bad_choice, dialogue_error = Dialogue.choose(bad_dialogue, bad_session, "bad", state)
  if Conditions.evaluate({ id = "met_mira" }, state)
      and not Conditions.evaluate({ id = "met_mira", operator = "greater", value = 3 }, state)
      and removed.count == 1 and quest_events[2].type == "quest_completed"
      and unavailable == nil and inventory_error.code == "INSUFFICIENT_ITEMS"
      and node.choices[1].id == "continue" and choice.next_node_id == "done"
      and bad_choice == nil and dialogue_error.code == "DIALOGUE_NEXT_NODE_NOT_FOUND"
      and bad_session.node_id == "start" then
    Save.set_string("data", "packages", "passed")
  end

  Events.subscribe("data_asset_reloaded", function(event)
    if event.id == "asset://data/probe" and event.old_revision == 1
        and event.new_revision == 2 then
      Save.set_string("data", "reload", "passed")
    end
  end)
end
return Probe
)lua")) {
    std::cerr << "Could not create Lua data fixtures.\n";
    return 1;
  }

  World world;
  InputState input;
  LuaScriptHost host;
  std::string error;
  if (!host.initialize(world, input, nullptr, error)) {
    std::cerr << "Lua host initialization failed: " << error << '\n';
    return 1;
  }
  auto registry = registryFor(root, "hash:1");
  host.setAssetRegistry(&registry);

  ProjectData project;
  project.name = "Lua Data Probe";
  project.projectDirectory = root;
  project.scriptEntry = "script://scripts/probe.lua";
  if (!host.loadWorldScripts(project, world, error)) {
    std::cerr << "Lua data probe failed to load: " << error << '\n';
    return 1;
  }
  host.start();
  if (host.saveString("data", "contract") != "passed" ||
      host.saveString("data", "packages") != "passed") {
    std::cerr << "Lua Data API or optional content package contract failed.\n";
    return 1;
  }

  if (!writeFile(root / "assets/probe.json",
                 R"({"format_version":1,"id":"probe","integer":8})"))
    return 1;
  registry = registryFor(root, "hash:2");
  host.setAssetRegistry(&registry);
  if (host.saveString("data", "reload") != "passed" ||
      host.dataAssetStore().revision("asset://data/probe") != 2) {
    std::cerr << "DataAsset hot reload event was not delivered.\n";
    return 1;
  }

  std::filesystem::remove_all(root, filesystemError);
  return 0;
}
