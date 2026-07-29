local AnimationSelector = {}

local choices = {
  { action = "animation_idle", key = "1", state = "idle", clip = "Idle_Loop" },
  { action = "animation_walk", key = "2", state = "walk", clip = "Walk_Loop" },
  { action = "animation_run", key = "3", state = "run", clip = "Sprint_Loop" },
  { action = "animation_jump", key = "4", state = "jump", clip = "Jump_Loop" },
  { action = "animation_dance", key = "5", state = "dance", clip = "Dance_Loop" },
}

local function choice_for_action(action)
  for _, choice in ipairs(choices) do
    if choice.action == action then
      return choice
    end
  end
  return nil
end

local function label_for(choice, selected)
  return (selected and "> " or "") .. "[" .. choice.key .. "] " .. string.upper(choice.state)
end

function AnimationSelector:select(choice)
  if not Animation.play(self.entity_id, choice.state) then
    Hud.set_text("subtitle", "MISSING CLIP: " .. choice.clip)
    return
  end
  for _, candidate in ipairs(choices) do
    Hud.set_button_label(candidate.action, label_for(candidate, candidate == choice))
  end
  Hud.set_text("subtitle", "PLAYING: " .. string.upper(choice.clip))
  Debug.log("Selected animation: " .. choice.clip)
end

function AnimationSelector:on_start()
  self:select(choices[1])
end

function AnimationSelector:on_update(_dt)
  for _, choice in ipairs(choices) do
    if Input.action_pressed(choice.action) then
      self:select(choice)
      return
    end
  end
end

-- HUD buttons dispatch actions; keyboard shortcuts are polled in on_update.
-- Keeping both paths pointed at select() ensures they cannot drift apart.
-- @HandleAction("animation_idle")
-- @HandleAction("animation_walk")
-- @HandleAction("animation_run")
-- @HandleAction("animation_jump")
-- @HandleAction("animation_dance")
function AnimationSelector:on_animation_action(event)
  local choice = choice_for_action(event.action)
  if choice then
    self:select(choice)
  end
end

return AnimationSelector
