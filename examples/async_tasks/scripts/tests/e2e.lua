local Test = require("demi.test")
local Hud = require("demi.hud")

return { tests = {
    {
        name = "HUD actions and sequential SQLite worker waits stay responsive",
        func = function()
            Test.expect_scene("scene://async_tasks/main")
            Test.wait(0.05)
            Test.expect(Hud.get_text("busy_status"):find("active", 1, true), "Busy task did not start")
            Test.touch("urgent")
            Test.expect(Hud.get_text("urgent_status") == "Urgent actions: 1", "Action was blocked by background work")
            Test.touch("database")
            local completed = false
            for attempt = 1, 120 do
                Test.wait(0.05)
                if Hud.get_text("database_status") == "SQLite query: completed / 42" then
                    completed = true
                    break
                end
            end
            Test.expect(completed, "SQLite worker failed or did not complete")
            Test.expect(Hud.get_text("busy_status"):find("active", 1, true), "SQLite worker stopped the ongoing calculation")
            Test.touch("urgent")
            Test.expect(Hud.get_text("urgent_status") == "Urgent actions: 2", "Action stopped responding")
            Test.touch("toggle_busy")
            for attempt = 1, 120 do
                if Hud.get_text("busy_status"):find("stopped", 1, true) then break end
                Test.wait(0.05)
            end
            Test.expect(Hud.get_text("busy_status"):find("stopped", 1, true), "Task cancellation did not stop the loop")
        end,
    },
} }
