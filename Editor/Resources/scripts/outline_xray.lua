-- outline_xray.lua
-- Demonstrates script-driven outline highlight and through-walls visibility.

local outline_color = Vec3(1.0, 0.5, 0.1)
local thickness = 2.5

function on_start(entity)
    entity:set_outline(true, outline_color, thickness, true)
end

function on_update(entity, dt)
    local pulse = 1.5 + math.sin(os.clock() * 3.0) * 0.75
    entity:set_outline(true, outline_color, pulse, true)
end

function on_destroy(entity)
    entity:set_outline(false, outline_color, 0.5, false)
end
