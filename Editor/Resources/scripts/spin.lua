-- spin.lua
-- Rotates an entity around the Y axis at a configurable speed.

local speed = 1.0
local angle = 0.0

function on_start(entity)
    -- Start from the entity's current yaw so we don't snap to 0
    local euler = entity:transform():get_rotation_euler()
    angle = euler.y
    print("[spin.lua] Attached to: " .. entity:name())
end

function on_update(entity, dt)
    angle = angle + dt * speed
    local t = entity:transform()
    t:set_rotation_euler(0, angle, 0)
end

function on_destroy(entity)
    print("[spin.lua] Detached from: " .. entity:name())
end
