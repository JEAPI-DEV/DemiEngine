#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scripting/LuaScriptHost.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <utility>

namespace {

using namespace demi::runtime;

bool writeFile(const std::filesystem::path &path, const char *contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  if (!output) {
    return false;
  }
  output << contents;
  return true;
}
} // namespace

int main() {
  namespace runtime = demi::runtime;

  const std::filesystem::path projectDirectory =
      std::filesystem::temp_directory_path() / "demi_lua_scripting_tests";
  std::error_code error;
  std::filesystem::remove_all(projectDirectory, error);
  std::filesystem::create_directories(projectDirectory / "scripts", error);
  if (error) {
    std::cerr << "Failed to create Lua test project directory.\n";
    return 1;
  }

  if (!writeFile(projectDirectory / "scripts" / "probe.lua", R"lua(
local Probe = {}
function Probe:on_start()
  require("action_module")
  Random.seed(1234)
  local exact_random_state = Random.state()
  if type(exact_random_state) == "string" and Random.restore(exact_random_state) then
    Save.set_string("test", "random_state", "exact")
  end
  Save.write("profile", { name = "Ada", coins = 7 }, 1)
  Save.register_migration(1, 2, function(state)
    state.level = 3
    return state
  end)
  local profile = Save.read("profile")
  if profile and profile.name == "Ada" and profile.coins == 7 and profile.level == 3 and Save.version("profile") == 2 then
    Save.set_string("test", "profile", "migrated")
  end
  local structured_ok = Save.write_state("campaign", {
    game = { score = 42 },
    selected_entities = { player = "ent_player" },
    prefab_instances = { enemy = "prefab://enemy" },
    lua = { quest = { stage = 2 } },
  }, { autosave = true, sequence = 7, reason = "checkpoint" })
  local campaign = Save.read_state("campaign")
  local metadata = Save.metadata("campaign")
  if structured_ok and campaign and campaign.game.score == 42 and metadata and metadata.autosave and metadata.sequence == 7 then
    Save.set_string("test", "structured_save", "passed")
  end
  Cutscene.play("cutscene://intro")
  if Cutscene.is_playing() and Cutscene.active() == "cutscene://intro" then
    Save.set_string("test", "cutscene", "playing")
  end
  Cutscene.pause()
  if not Cutscene.is_playing() then
    Save.set_string("test", "cutscene_paused", "true")
  end
  Cutscene.resume()
  Cutscene.skip()
  Events.subscribe("hud_action", function(event)
    Save.set_string("test", "hud_action", event.action .. ":" .. event.id)
  end)
  Events.emit("test.script_event", { value = "script" })
  Events.emit("test.module_event", { value = "module" })
  if Hud.set_button_label("button_start", "GO") then
    Save.set_string("test", "button_label", "updated")
  end
  Hud.text("hud_probe", "probe", 8.0, 12.0, 2.0)
  if Hud.set_text_scale("hud_probe", 5.5) then
    Save.set_string("test", "hud_text_scale", "updated")
  end
  if Hud.set_position("hud_image", 18.0, 24.0) and Hud.set_image_animation_frame("hud_image", "asset://animations/test", 3) and Hud.set_size("button_start", 40.0, 90.0) and Hud.set_opacity("button_start", 0.25) then
    Save.set_string("test", "hud_animation_properties", "updated")
  end
  Entity.create("ent_tinted_sprite", {
    components = {
      Transform2D = {
        position = { 0.0, 0.0 },
      },
      Sprite = {
        texture = "asset://sprites/test",
        color = { 0.10, 0.20, 0.30, 0.40 },
      },
    },
  })
  if Entity.set_sprite_color("ent_tinted_sprite", 0.45, 0.55, 0.65, 0.75) and
      Sprite2D.set_size("ent_tinted_sprite", 2.5, 0.5) then
    Save.set_string("test", "sprite_color", "updated")
  end
  Entity.create("ent_iso_parent", {
    components = {
      IsoTransform = { tile = { 1.0, 2.0 }, height = 0.5 },
    },
  })
  Entity.create("ent_iso_created", {
    components = {
      IsoTransform = {
        parent = "ent_iso_parent",
        tile = { 4.0, 7.0 },
        height = 0.25,
        footprint = { 2.0, 1.0 },
      },
      Buildable = {
        asset = "asset://buildings/test",
        blocks_movement = true,
      },
    },
  })
  local roundtrip = Network.decode(Network.encode("color_probe", {
    color = { 0.45, 0.55, 0.65, 0.75 },
  }))
  if roundtrip ~= nil and roundtrip.payload.color[1] == 0.45 and roundtrip.payload.color[4] == 0.75 then
    Save.set_string("test", "network_array_roundtrip", "passed")
  end
  if Runtime.platform() == "linux" then
    Save.set_string("test", "runtime_platform", "linux")
  end
  local http_probe = Network.http_get("ftp://simplehardware.net/lobby.php")
  if http_probe and http_probe.ok == false and http_probe.status == 0 and string.find(http_probe.error, "URL") then
    Save.set_string("test", "network_http_probe", "passed")
  end
  local lobby_probe = Network.lobby_list("ftp://simplehardware.net/lobby.php", "minimal_2d_android")
  if lobby_probe and lobby_probe.ok == false and lobby_probe.status == 0 and string.find(lobby_probe.error, "URL") then
    Save.set_string("test", "network_lobby_probe", "passed")
  end
  Runtime.set_max_fps(144)
  if Runtime.get_max_fps() == 144 then
    Save.set_number("test", "max_fps", 144)
  end
end
-- @HandleAction("test.annotated")
function Probe:handle_annotated_action(event)
  Save.set_string("test", "annotated_action", event.action .. ":" .. event.id)
end
-- @OnEvent("test.script_event")
function Probe:handle_script_event(event)
  Save.set_string("test", "script_event", event.value)
end
function Probe:on_update(dt)
  if Input.is_down("space") then
    Save.set_string("test", "space", "down")
  else
    Save.set_string("test", "space", "up")
  end
  if Input.is_pressed("escape") then
    Save.set_string("test", "escape_pressed", "pressed")
  else
    Save.set_string("test", "escape_pressed", "released")
  end
end
return Probe
)lua")) {
    std::cerr << "Failed to write probe.lua.\n";
    return 1;
  }

  if (!writeFile(projectDirectory / "scripts" / "action_module.lua", R"lua(
local ActionModule = {}
-- @HandleAction("test.module")
function ActionModule.handle_action(event)
  Save.set_string("test", "module_action", event.action .. ":" .. event.id)
end
-- @OnEvent("test.module_event")
function ActionModule.handle_event(event)
  Save.set_string("test", "module_event", event.value)
end
return ActionModule
)lua")) {
    std::cerr << "Failed to write action_module.lua.\n";
    return 1;
  }

  if (!writeFile(projectDirectory / "scripts" / "button.lua", R"lua(
local Button = {}
function Button:on_ui_click(event)
  Save.set_string("test", "clicked", event.id)
end
return Button
)lua")) {
    std::cerr << "Failed to write button.lua.\n";
    return 1;
  }

  if (!writeFile(projectDirectory / "scripts" / "prop_probe.lua", R"lua(
local PropProbe = {}
function PropProbe:on_start()
  if self.entity_id == "ent_prop" and self.speed == 12.5 and self.enabled == true and self.tags[1] == "runner" and self.spawn.x == 3.0 then
    Save.set_string("test", "script_properties", "generic")
  end
end
return PropProbe
)lua")) {
    std::cerr << "Failed to write prop_probe.lua.\n";
    return 1;
  }

  runtime::ProjectData project;
  project.projectDirectory = projectDirectory;
  project.scriptEntry = "script://scripts/probe.lua";
  project.scriptModules = {"action_module"};

  runtime::World world;
  world.hudCanvasSize = runtime::Vec2{.x = 100.0F, .y = 100.0F};
  runtime::Entity propEntity;
  propEntity.id = "ent_prop";
  propEntity.name = "Property Probe";
  propEntity.setComponent<LuaScriptComponent>(runtime::LuaScriptComponent{
      .module = "script://scripts/prop_probe.lua",
      .propertiesJson =
          R"json({ "enabled": true, "speed": 12.5, "tags": ["runner"], "spawn": { "x": 3.0, "y": 4.0 } })json",
  });
  world.entities.push_back(std::move(propEntity));
  runtime::ui::UiNode buttonStart;
  buttonStart.id = "button_start";
  buttonStart.type = "button";
  buttonStart.text = "START";
  buttonStart.layout.position = runtime::Vec2{.x = 0.0F, .y = 0.0F};
  buttonStart.layout.size = runtime::Vec2{.x = 45.0F, .y = 100.0F};
  buttonStart.script = "script://scripts/button.lua";
  buttonStart.action = "test.annotated";
  world.ui.nodes.push_back(buttonStart);
  runtime::ui::UiNode hudImage;
  hudImage.id = "hud_image";
  hudImage.type = "image";
  hudImage.layout.position = runtime::Vec2{.x = 0.0F, .y = 0.0F};
  hudImage.layout.size = runtime::Vec2{.x = 8.0F, .y = 8.0F};
  world.ui.nodes.push_back(hudImage);
  runtime::ui::UiNode buttonModule;
  buttonModule.id = "button_module";
  buttonModule.type = "button";
  buttonModule.text = "MODULE";
  buttonModule.layout.position = runtime::Vec2{.x = 55.0F, .y = 0.0F};
  buttonModule.layout.size = runtime::Vec2{.x = 45.0F, .y = 100.0F};
  buttonModule.action = "test.module";
  world.ui.nodes.push_back(buttonModule);
  runtime::Entity mover3D;
  mover3D.id = "ent_3d_mover";
  mover3D.name = "3D Mover";
  mover3D.setComponent<Transform3DComponent>(runtime::Transform3DComponent{
      .position = runtime::Vec3{.x = 0.0F, .y = 0.5F, .z = 0.0F}});
  mover3D.setComponent<BoxCollider3DComponent>(runtime::BoxCollider3DComponent{
      .size = runtime::Vec3{.x = 1.0F, .y = 1.0F, .z = 1.0F}});
  mover3D.setComponent<Rigidbody3DComponent>(runtime::Rigidbody3DComponent{
      .bodyType = "dynamic", .useGravity = false});
  world.entities.push_back(std::move(mover3D));
  runtime::Entity wall3D;
  wall3D.id = "ent_3d_wall";
  wall3D.name = "3D Wall";
  wall3D.setComponent<Transform3DComponent>(runtime::Transform3DComponent{
      .position = runtime::Vec3{.x = 1.25F, .y = 0.5F, .z = 0.0F}});
  wall3D.setComponent<BoxCollider3DComponent>(runtime::BoxCollider3DComponent{
      .size = runtime::Vec3{.x = 1.0F, .y = 1.0F, .z = 1.0F}});
  wall3D.setComponent<Rigidbody3DComponent>(
      runtime::Rigidbody3DComponent{.bodyType = "static", .useGravity = false});
  world.entities.push_back(std::move(wall3D));
  runtime::Entity sphere3D;
  sphere3D.id = "ent_3d_sphere";
  sphere3D.name = "3D Sphere";
  sphere3D.setComponent<Transform3DComponent>(runtime::Transform3DComponent{
      .position = runtime::Vec3{.x = -1.25F, .y = 0.5F, .z = 0.0F}});
  sphere3D.setComponent<SphereCollider3DComponent>(
      runtime::SphereCollider3DComponent{.radius = 0.5F});
  sphere3D.setComponent<Rigidbody3DComponent>(
      runtime::Rigidbody3DComponent{.bodyType = "static", .useGravity = false});
  world.entities.push_back(std::move(sphere3D));
  runtime::Entity child3D;
  child3D.id = "ent_3d_child";
  child3D.name = "3D Child";
  child3D.setComponent<Transform3DComponent>(runtime::Transform3DComponent{
      .parent = "ent_3d_mover",
      .position = runtime::Vec3{.x = 0.0F, .y = 2.0F, .z = 0.0F}});
  world.entities.push_back(std::move(child3D));
  runtime::Entity parent2D;
  parent2D.id = "ent_2d_parent";
  parent2D.name = "2D Parent";
  parent2D.setComponent<Transform2DComponent>(runtime::Transform2DComponent{
      .position = runtime::Vec2{.x = 3.0F, .y = 4.0F}});
  world.entities.push_back(std::move(parent2D));
  runtime::Entity child2D;
  child2D.id = "ent_2d_child";
  child2D.name = "2D Child";
  child2D.setComponent<Transform2DComponent>(runtime::Transform2DComponent{
      .parent = "ent_2d_parent",
      .position = runtime::Vec2{.x = 1.0F, .y = 2.0F}});
  world.entities.push_back(std::move(child2D));

  runtime::InputState input;
  input.mousePosition = runtime::Vec2{.x = 25.0F, .y = 50.0F};

  runtime::LuaScriptHost host;
  std::string luaError;
  if (!host.initialize(world, input, nullptr, luaError)) {
    std::cerr << "Lua host failed to initialize: " << luaError << '\n';
    return 1;
  }
  if (!host.loadWorldScripts(project, world, luaError)) {
    std::cerr << "Lua scripts failed to load: " << luaError << '\n';
    return 1;
  }

  host.setViewport(100, 100);
  host.start();
  if (host.saveString("test", "profile") != "migrated") {
    std::cerr
        << "Save.read/write migration hook did not migrate profile data.\n";
    return 1;
  }
  if (host.saveString("test", "structured_save") != "passed") {
    std::cerr << "Structured game-state Lua save did not round-trip.\n";
    return 1;
  }
  if (host.saveString("test", "random_state") != "exact") {
    std::cerr << "Lua deterministic random state was not string-safe.\n";
    return 1;
  }
  if (host.saveString("test", "cutscene") != "playing" ||
      host.saveString("test", "cutscene_paused") != "true") {
    std::cerr
        << "Cutscene Lua API did not report expected state transitions.\n";
    return 1;
  }
  if (host.saveNumber("test", "max_fps").value_or(0.0F) != 144.0F) {
    std::cerr << "Runtime max FPS Lua API did not persist expected value.\n";
    return 1;
  }
  if (host.saveString("test", "script_properties") != "generic") {
    std::cerr << "Generic LuaScript properties were not copied into Lua script "
                 "fields.\n";
    return 1;
  }
  const auto buttonStartNode = std::ranges::find_if(
      world.ui.nodes, [](const runtime::ui::UiNode &element) {
        return element.id == "button_start";
      });
  if (host.saveString("test", "button_label") != "updated" ||
      buttonStartNode == world.ui.nodes.end() ||
      buttonStartNode->text != "GO") {
    std::cerr << "Hud.set_button_label did not update the HUD button label.\n";
    return 1;
  }
  const auto hudProbe = std::ranges::find_if(
      world.ui.nodes, [](const runtime::ui::UiNode &element) {
        return element.id == "hud_probe";
      });
  if (host.saveString("test", "hud_text_scale") != "updated" ||
      hudProbe == world.ui.nodes.end() || hudProbe->fontSize != 44.0F) {
    std::cerr << "Hud.set_text_scale did not update the HUD text scale.\n";
    return 1;
  }
  if (host.saveString("test", "hud_animation_properties") != "updated" ||
      buttonStartNode == world.ui.nodes.end() ||
      buttonStartNode->layout.size.x != 40.0F ||
      buttonStartNode->layout.size.y != 90.0F ||
      buttonStartNode->color.a != 0.25F ||
      buttonStartNode->hoverColor.a != 0.25F ||
      buttonStartNode->textColor.a != 0.25F) {
    std::cerr << "HUD animation property setters did not update the expected "
                 "elements.\n";
    return 1;
  }
  const runtime::Entity *tintedSprite =
      runtime::findEntity(world, "ent_tinted_sprite");
  if (host.saveString("test", "sprite_color") != "updated" ||
      tintedSprite == nullptr ||
      !tintedSprite->hasComponent<SpriteComponent>() ||
      tintedSprite->component<SpriteComponent>()->color.r != 0.45F ||
      tintedSprite->component<SpriteComponent>()->color.g != 0.55F ||
      tintedSprite->component<SpriteComponent>()->color.b != 0.65F ||
      tintedSprite->component<SpriteComponent>()->color.a != 0.75F ||
      tintedSprite->component<SpriteComponent>()->size.x != 2.5F ||
      tintedSprite->component<SpriteComponent>()->size.y != 0.5F) {
    std::cerr
        << "Sprite color Lua API did not create and update a tinted sprite.\n";
    return 1;
  }
  const runtime::Entity *isoCreated =
      runtime::findEntity(world, "ent_iso_created");
  const runtime::IsoTransformComponent resolvedIso =
      isoCreated == nullptr ? runtime::IsoTransformComponent{}
                            : runtime::worldIsoTransform(world, *isoCreated);
  if (isoCreated == nullptr ||
      !isoCreated->hasComponent<IsoTransformComponent>() ||
      !isoCreated->hasComponent<BuildableComponent>() ||
      isoCreated->component<IsoTransformComponent>()->tile.x != 4.0F ||
      isoCreated->component<IsoTransformComponent>()->tile.y != 7.0F ||
      isoCreated->component<IsoTransformComponent>()->parent !=
          "ent_iso_parent" ||
      resolvedIso.tile.x != 5.0F || resolvedIso.tile.y != 9.0F ||
      resolvedIso.height != 0.75F ||
      isoCreated->component<IsoTransformComponent>()->footprint.x != 2.0F ||
      !isoCreated->component<BuildableComponent>()->blocksMovement) {
    std::cerr << "Entity.create did not delegate authored isometric "
                 "components to the component registry.\n";
    return 1;
  }
  if (host.saveString("test", "network_array_roundtrip") != "passed") {
    std::cerr << "Network Lua message encoding did not preserve array-style "
                 "tables.\n";
    return 1;
  }
  if (host.saveString("test", "network_http_probe") != "passed") {
    std::cerr << "Network Lua HTTP API did not return the expected unsupported "
                 "TLS error.\n";
    return 1;
  }
  if (host.saveString("test", "network_lobby_probe") != "passed") {
    std::cerr
        << "Network Lua lobby API did not share HTTP validation behavior.\n";
    return 1;
  }
  if (host.saveString("test", "script_event") != "script") {
    std::cerr << "Script @OnEvent did not handle emitted event.\n";
    return 1;
  }
  if (host.saveString("test", "module_event") != "module") {
    std::cerr
        << "Project-listed module @OnEvent did not handle emitted event.\n";
    return 1;
  }
  if (!host.setEntityPosition3D("ent_3d_mover", 1.0F, 0.5F, 0.0F)) {
    std::cerr << "Transform3D.set_position failed for test mover.\n";
    return 1;
  }
  const std::optional<runtime::Vec3> moved3D =
      host.entityPosition3D("ent_3d_mover");
  if (!moved3D.has_value() || moved3D->x != 0.0F) {
    std::cerr << "3D dynamic mover passed through a static BoxCollider3D.\n";
    return 1;
  }
  if (!host.setEntityPosition3D("ent_3d_mover", -1.0F, 0.5F, 0.0F)) {
    std::cerr << "Transform3D.set_position failed for sphere test mover.\n";
    return 1;
  }
  const std::optional<runtime::Vec3> sphereBlocked3D =
      host.entityPosition3D("ent_3d_mover");
  if (!sphereBlocked3D.has_value() || sphereBlocked3D->x != 0.0F) {
    std::cerr << "3D dynamic mover passed through a static SphereCollider3D.\n";
    return 1;
  }
  const runtime::Entity *child = runtime::findEntity(world, "ent_3d_child");
  if (child == nullptr || runtime::worldPosition3D(world, *child).y != 2.5F) {
    std::cerr << "Transform3D parent world position did not include parent "
                 "transform.\n";
    return 1;
  }
  const runtime::Entity *child2DLookup =
      runtime::findEntity(world, "ent_2d_child");
  if (child2DLookup == nullptr ||
      runtime::worldPosition2D(world, *child2DLookup).x != 4.0F ||
      runtime::worldPosition2D(world, *child2DLookup).y != 6.0F) {
    std::cerr << "Transform2D parent world position did not include parent "
                 "transform.\n";
    return 1;
  }
  host.update(1.0F / 60.0F);
  if (host.saveString("test", "space") != "up") {
    std::cerr << "Input.is_down returned true for an unpressed key.\n";
    return 1;
  }
  input.keysPressed.insert("escape");
  host.update(1.0F / 60.0F);
  if (host.saveString("test", "escape_pressed") != "pressed") {
    std::cerr << "Input.is_pressed did not report a pressed key.\n";
    return 1;
  }
  input.keysPressed.clear();
  host.update(1.0F / 60.0F);
  if (host.saveString("test", "escape_pressed") != "released") {
    std::cerr << "Input.is_pressed stayed true after the press frame.\n";
    return 1;
  }

  input.mouseButtonsDown.insert("left");
  host.update(1.0F / 60.0F);
  if (host.saveString("test", "clicked") != "button_start") {
    std::cerr << "HUD button click did not reach Lua on_ui_click.\n";
    return 1;
  }
  if (host.saveString("test", "hud_action") != "test.annotated:button_start") {
    std::cerr << "HUD button action annotation did not emit hud_action.\n";
    return 1;
  }
  if (host.saveString("test", "annotated_action") !=
      "test.annotated:button_start") {
    std::cerr << "@HandleAction did not dispatch to annotated Lua function.\n";
    return 1;
  }

  input.mouseButtonsDown.clear();
  host.update(1.0F / 60.0F);
  input.mousePosition = runtime::Vec2{.x = 75.0F, .y = 50.0F};
  input.mouseButtonsDown.insert("left");
  host.update(1.0F / 60.0F);
  if (host.saveString("test", "module_action") != "test.module:button_module") {
    std::cerr << "Project-listed Lua module @HandleAction did not dispatch.\n";
    return 1;
  }

  host.destroy();
  std::filesystem::remove_all(projectDirectory, error);
  return 0;
}
