-- pulse_glow.lua
-- Enables emissive glow and pulses intensity over time.

local min_intensity = 0.25
local max_intensity = 2.0
local speed = 2.5
local time = 0.0

function on_start(entity)
    if not entity:has_mesh() then
        print("[pulse_glow.lua] Entity has no mesh: " .. entity:name())
        return
    end

    entity:set_glow(true, Vec3(0.2, 0.8, 1.0), min_intensity)
    print("[pulse_glow.lua] Attached to: " .. entity:name())
end

function on_update(entity, dt)
    if not entity:has_mesh() then
        return
    end

    time = time + dt * speed
    local t = (math.sin(time) * 0.5) + 0.5
    entity:set_glow(true, Vec3(0.2, 0.8, 1.0), min_intensity + (max_intensity - min_intensity) * t)
end

function on_destroy(entity)
    if entity:has_mesh() then
        entity:set_glow(false, Vec3(0.2, 0.8, 1.0), 0.0)
    end

    print("[pulse_glow.lua] Detached from: " .. entity:name())
end
