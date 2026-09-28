local Task = require("demi.task")
local Shared = require("demi.shared")
local Hud = require("demi.hud")
local Input = require("demi.input")
local Transform = require("demi.transform2d")

local Demo = {}

function Demo:on_start()
    self.urgent_count = 0
    self.progress = Shared.map({ iterations = 0 })
    self:on_toggle_busy()
end

-- @HandleAction("toggle_busy")
function Demo:on_toggle_busy()
    if self.busy and not self.busy:done() then
        self.busy:cancel()
        return
    end
    local progress = self.progress -- Explicit native handle, not the main VM's self.
    progress:set("iterations", 0)
    self.busy = Task.fork(function()
        local Task = require("demi.task")
        local iterations = 0
        while not Task.cancelled() do
            for i = 1, 100000 do iterations = iterations + 1 end
            progress:set("iterations", iterations)
        end
        return iterations
    end)
end

-- @HandleAction("urgent")
function Demo:on_urgent()
    self.urgent_count = self.urgent_count + 1
    Hud.set_text("urgent_status", "Urgent actions: " .. self.urgent_count)
end

-- @HandleAction("database")
function Demo:on_database()
    if self.database_task then return end
    self.database_task = Task.fork(function()
        local Database = require("demi.database")
        local connection, err = Database.open({ driver = "sqlite", path = ":memory:" })
        assert(connection, err)
        local result, query_error = connection:query(
            "SELECT ? AS label, ? AS number", { "completed", 42 }):wait()
        local closed, close_error = connection:close():wait()
        assert(result, query_error)
        assert(closed, close_error)
        return result.rows[1]
    end)
    Hud.set_text("database_status", "SQLite query: working")
end

function Demo:on_update(dt)
    if Input.pressed("confirm") then self:on_urgent() end
    local status = self.busy and not self.busy:done() and "active" or "stopped"
    local iterations = self.progress:get("iterations")
    Hud.set_text("busy_status", "Worker: " .. status .. " | iterations: " .. tostring(iterations or "busy"))
    local task = self.database_task
    if not task or not task:done() then return end
    local row, err = task:result()
    self.database_task = nil
    if task:status() == "completed" then
        Hud.set_text("database_status", "SQLite query: " .. row[1] .. " / " .. row[2])
    else
        Hud.set_text("database_status", "SQLite: " .. tostring(err))
    end
end

function Demo:on_fixed_update(dt)
    local x, y = Input.value("move_x"), Input.value("move_y")
    local length = math.max(1, math.sqrt(x * x + y * y))
    Transform.add_position(self.entity_id, x / length * dt * 3, y / length * dt * 3)
end

function Demo:on_destroy()
    if self.busy then self.busy:cancel() end
    if self.database_task then self.database_task:cancel() end
end

return Demo
