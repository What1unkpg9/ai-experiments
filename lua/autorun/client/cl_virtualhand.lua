-- GMod Hand Tracker / client-only visualizer
-- Requires embedded DHTML to support ws://127.0.0.1:8765.
local MODEL = "models/hunter/blocks/cube025x025x025.mdl"
local DISTANCE, HEIGHT = 80, -15
local MODEL_SCALE, SMOOTH_SPEED, TIMEOUT = 0.23, 20, 0.5

local cubes, targets = {}, {}
local bridge, lastPacket = nil, 0
local detected, resetPosition = false, true

local function hide()
    for _, cube in ipairs(cubes) do
        if IsValid(cube) then cube:SetNoDraw(true) end
    end
end

local function createCubes()
    if #cubes == 21 then return end
    for i = 1, 21 do
        local cube = ClientsideModel(MODEL, RENDERGROUP_OPAQUE)
        if not IsValid(cube) then return end
        cube:SetModelScale(MODEL_SCALE, 0)
        cube:SetNoDraw(true)
        if i == 1 then
            cube:SetColor(Color(255, 70, 70))
        elseif i == 5 or i == 9 or i == 13 or i == 17 or i == 21 then
            cube:SetColor(Color(255, 210, 40))
        else
            cube:SetColor(Color(30, 180, 255))
        end
        cubes[i] = cube
    end
end

local function validNumber(n)
    return isnumber(n) and n == n and math.abs(n) <= 200
end

local function receiveHand(json)
    local data = util.JSONToTable(json)
    if not istable(data) then return end
    lastPacket = RealTime()
    if data.detected ~= true or not istable(data.points) or #data.points ~= 21 then
        detected = false
        resetPosition = true
        hide()
        return
    end

    local incoming = {}
    for i = 1, 21 do
        local p = data.points[i]
        if not istable(p) or not validNumber(p[1]) or not validNumber(p[2])
            or not validNumber(p[3]) then
            detected = false
            resetPosition = true
            hide()
            return
        end
        incoming[i] = Vector(p[1], p[2], p[3])
    end
    if not detected then resetPosition = true end
    targets = incoming
    detected = true
    createCubes()
end

local function createBridge()
    if IsValid(bridge) then return end
    bridge = vgui.Create("DHTML")
    bridge:SetSize(1, 1)
    bridge:SetPos(0, 0)
    bridge:SetMouseInputEnabled(false)
    bridge:SetKeyboardInputEnabled(false)

    -- Register bridge BEFORE loading the document.
    bridge:AddFunction("gmod", "receiveHand", receiveHand)
    bridge:AddFunction("gmod", "status", function(message)
        print("[VirtualHand] " .. tostring(message))
    end)

    -- Avoid external documents; all processing stays on localhost.
    bridge:SetHTML([=[
<!doctype html><html><head><meta charset="utf-8"></head><body>
<script>
(function () {
  let socket = null;
  function connect() {
    try {
      socket = new WebSocket('ws://127.0.0.1:8765');
      socket.onopen = () => gmod.status('Connected');
      socket.onmessage = e => gmod.receiveHand(e.data);
      socket.onerror = () => {};
      socket.onclose = () => {
        gmod.status('Disconnected, reconnecting...');
        setTimeout(connect, 2000);
      };
    } catch (e) {
      gmod.status('WebSocket error: ' + e);
      setTimeout(connect, 2000);
    }
  }
  connect();
})();
</script></body></html>
]=])
end

hook.Add("Think", "GModHandTracker_Update", function()
    if not IsValid(bridge) then createBridge() end
    if RealTime() - lastPacket > TIMEOUT then
        if detected then
            detected = false
            resetPosition = true
            hide()
        end
        return
    end
    if not detected then return end
    local ply = LocalPlayer()
    if not IsValid(ply) then return end
    local ang = ply:EyeAngles()
    local base = ply:EyePos() + ang:Forward() * DISTANCE + ang:Up() * HEIGHT
    local alpha = math.Clamp(FrameTime() * SMOOTH_SPEED, 0, 1)
    for i = 1, 21 do
        local cube, point = cubes[i], targets[i]
        if IsValid(cube) and point then
            local pos = base + ang:Right() * point.x
                + ang:Up() * point.y + ang:Forward() * point.z
            cube:SetPos(resetPosition and pos or LerpVector(alpha, cube:GetPos(), pos))
            cube:SetNoDraw(false)
        end
    end
    resetPosition = false
end)

hook.Add("ShutDown", "GModHandTracker_Cleanup", function()
    for _, cube in ipairs(cubes) do
        if IsValid(cube) then cube:Remove() end
    end
    if IsValid(bridge) then bridge:Remove() end
end)

print("[VirtualHand] Loaded")
