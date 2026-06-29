Scene Runtime Model Implement actual runtime entities/components instead of just scanning JSON for draw placeholders.
Add World
Add EntityId
Add components like Transform2D, Camera2D, Sprite, IsoGrid, HitboxController
Load scene JSON into typed C++ structures
Keep validation errors structured
Better Renderer Layer Keep SDL3 for now, but isolate rendering behind an engine API.
Add Renderer2D
Add camera transform/zoom
Add draw commands: rect, line, grid, sprite placeholder
Add resize handling
Add optional debug overlays
Prepare the API so bgfx can replace SDL drawing later without changing gameplay code
Input And Movement Make the scene interactive.
Add keyboard input
Move the player rectangle with WASD/arrow keys
Add Escape to quit
Add frame delta time
Add simple fixed-update loop structure
Lua First Slice Embed Lua 5.4 and bind a tiny initial API.
Load scripts/player.lua
Call on_create, on_start, on_update(dt), on_destroy
Expose Debug.log
Then expose basic movement helpers like Entity.set_position
Generate a first LuaLS stub file
Editor Preview Step Start replacing the placeholder editor with a basic window.
Open an SDL3/Dear ImGui editor shell
Dock a Scene view and Game view later
First milestone: editor launches and lists project/scene info
Then embed the same runtime viewport
Save/Load Slice Add a simple save command and runtime save API.
demi save inspect <file>
Validate save files more strictly
Runtime can write player position into JSON
Runtime can load player position from save
Recommended Next Vertical Slice The best next task is:

Implement typed scene loading + input movement + Lua on_update