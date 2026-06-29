Phase 4 Goal Turn the current prototype runtime into a more engine-like foundation: assets, real sprite loading, stronger Lua APIs, and basic editor/runtime separation.

Recommended Phase 4 scope:

Asset Pipeline MVP
Add *.asset.json manifests.
Add demi asset inspect <asset>.
Add demi asset deps <asset>.
Add texture asset type.
Resolve asset://sprites/player.png through the project asset registry.
Validate missing asset references.
Real Sprite Rendering
Replace blue placeholder rectangles with real texture loading.
Use SDL3 image loading first or simple BMP/PNG support depending available packages.
Sprite.texture should load from asset://....
Keep fallback rectangle rendering if texture load fails, but emit a diagnostic.
Lua API Expansion
Add Entity.set_position(entity_id, x, y).
Add Entity.find(name_or_id).
Add Time.delta_time.
Add Scene.load(scene_id) as a planned stub or basic command.
Improve script error messages with entity ID, module path, lifecycle function.
Input API Cleanup
Formalize key names.
Add Input.axis(negative_key, positive_key).
Add Input.vector(left, right, down, up).
Keep controls script-owned.
Scene Validation Upgrade
Validate component shapes more strictly.
Validate required component fields.
Validate LuaScript.module exists.
Validate Sprite.texture asset reference exists.
Validate duplicate entity IDs.
Save/Load Runtime Slice
Add Save.write(slot, table) and Save.read(slot) later.
For Phase 4 MVP, add C++ command:
demi save inspect
demi save write-position --project ... --entity ent_player --slot smoke
Or simpler: runtime key F5 save, F9 load, implemented through generic engine save APIs.
Editor Phase Prep
Replace editor placeholder with basic SDL3/Dear ImGui window if Dear ImGui is available or vendored.
First editor milestone:
project name
scene name
entity list
selected entity component summary
Do not implement game-specific editor panels yet.
Best Next Vertical Slice Implement this first:

Asset manifests + texture loading + Sprite.texture rendering + validation for missing asset/script references

Why:

It removes the “blue cube placeholder” concern.
It strengthens the engine/content separation.
It gives the project real visible game content.
It prepares the renderer for actual 2D games.
After that, do:

Expanded Lua API + stricter scene validation

Then:

Basic editor shell/entity inspector