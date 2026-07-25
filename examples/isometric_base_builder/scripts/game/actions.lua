local Actions = {}
local context = nil
function Actions.bind(value) context = value end
-- @HandleAction("build_arrow")
function Actions.build_arrow() context.building.select("arrow") end
-- @HandleAction("build_wizard")
function Actions.build_wizard() context.building.select("wizard") end
-- @HandleAction("start_wave")
function Actions.start_wave() context.waves.start() end
-- @HandleAction("tower_upgrade")
function Actions.tower_upgrade() context.building.open_upgrade_menu() end
-- @HandleAction("tower_destroy")
function Actions.tower_destroy() context.building.destroy_selected() end
-- @HandleAction("tower_target_random")
function Actions.tower_target_random() context.building.set_targeting("random") end
-- @HandleAction("tower_target_strongest")
function Actions.tower_target_strongest() context.building.set_targeting("strongest") end
-- @HandleAction("tower_target_weakest")
function Actions.tower_target_weakest() context.building.set_targeting("weakest") end
-- @HandleAction("tower_target_first")
function Actions.tower_target_first() context.building.set_targeting("first") end
-- @HandleAction("upgrade_range")
function Actions.upgrade_range() context.building.upgrade("range") end
-- @HandleAction("upgrade_power")
function Actions.upgrade_power() context.building.upgrade("power") end
return Actions
