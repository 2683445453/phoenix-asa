local ImGui, SDK, PC, Character, System, Actor = ImGui, SDK, PC, Character, System, Actor
local floor, min, format, tostring = math.floor, math.min, string.format, tostring
local os_date = os.date

local STR_POOL_PERCENT = {}
local STR_POOL_INT = {}
for i = 0, 1000 do
    if i <= 100 then STR_POOL_PERCENT[i] = i .. "%" end
    STR_POOL_INT[i] = tostring(i)
end

local WatermarkRenderer = {
    DEFAULT_CONFIG = {
        fade = 40, padX = -10, padY = 5, alpha = 50, shadowA = 127, lerpSpeed = 10.0,
        defaultColor = {r = 255, g = 255, b = 255, a = 200}
    },
    _state = {
        lastUpdateTimer = 1.0,
        updateRate = 1.0,
        currentWidth = 0,
        currentHeight = 0,
        targetHeight = 0,
        isInitialized = false,
        lastColor = {r = 0, g = 0, b = 0, a = 0}
    }
}

local COL_WHITE  = ImGui.Color(255, 255, 255, 200)
local COL_Accent = ImGui.Color(Client.GetMenuColor())
local COL_SHADOW = nil
local COL_BG     = nil
local COL_TRANSPARENT = ImGui.Color(0, 0, 0, 0)

local staticSegments = {
    { text = "Begeerte     ", color = COL_WHITE, _w = 0 },
    { text = "0", color = COL_Accent, _w = 0 },         -- [2] FPS
    { text = " FPS    ", color = COL_WHITE, _w = 0 },
    { text = "0", color = COL_Accent, _w = 0 },         -- [4] PING
    { text = " PING    ", color = COL_WHITE, _w = 0 },
    { text = "0%", color = COL_Accent, _w = 0 },        -- [6] CPU%
    { text = " CPU    ", color = COL_WHITE, _w = 0 },
    { text = "0.0", color = COL_Accent, _w = 0 },       -- [8] GHZ
    { text = " GHZ    ", color = COL_WHITE, _w = 0 },
    { text = "0%", color = COL_Accent, _w = 0 },        -- [10] GPU%
    { text = " GPU    ", color = COL_WHITE, _w = 0 },
    { text = "0", color = COL_Accent, _w = 0 },         -- [12] Actors
    { text = " Actors    ", color = COL_WHITE, _w = 0 },
    { text = "00:00:00", color = COL_WHITE, _w = 0 }    -- [14] Time
}

function WatermarkRenderer.DrawMultiColor(segments, x, y)
    local cfg = WatermarkRenderer.DEFAULT_CONFIG
    local state = WatermarkRenderer._state
    
    if not COL_SHADOW then
        COL_SHADOW = ImGui.Color(0, 0, 0, cfg.shadowA)
        COL_BG = ImGui.Color(0, 0, 0, cfg.alpha)
    end

    local targetTextW = 0
    for i=1, #segments do
        targetTextW = targetTextW + segments[i]._w
    end

    local targetW = targetTextW + cfg.padX * 2
    local targetH = state.targetHeight + cfg.padY * 2
    local dt = ImGui.GetDeltaTime()

    if not state.isInitialized then
        state.currentWidth, state.currentHeight = targetW, targetH
        state.isInitialized = true
    else
        local lerpFactor = min(1.0, dt * cfg.lerpSpeed)
        state.currentWidth = state.currentWidth + (targetW - state.currentWidth) * lerpFactor
        state.currentHeight = state.currentHeight + (targetH - state.currentHeight) * lerpFactor
    end

    local x0 = floor(x + (targetW - state.currentWidth))
    local x1, y1 = x0 + cfg.fade, floor(y)
    local x2, y2 = x1 + state.currentWidth, y1 + floor(state.currentHeight)
    local x3 = x2 + cfg.fade

    ImGui.AddRectFilledMultiColor(x0, y1, x1, y2, COL_TRANSPARENT, COL_BG, COL_BG, COL_TRANSPARENT)
    ImGui.AddRectFilled(x1, y1, x2, y2, COL_BG, 0)
    ImGui.AddRectFilledMultiColor(x2, y1, x3, y2, COL_BG, COL_TRANSPARENT, COL_TRANSPARENT, COL_BG)

    local curX = x1 + cfg.padX
    local curY = y1 + cfg.padY
    for i=1, #segments do
        local seg = segments[i]
        local txt = seg.text
        ImGui.AddText(curX + 1, curY + 1, COL_SHADOW, txt)
        ImGui.AddText(curX, curY, seg.color, txt)
        curX = curX + seg._w
    end
end

local CHAR_CLASS, DINO_CLASS

function Main()
    local screenSize = ImGui.GetScreenSize()
    if not screenSize then return end

    local state = WatermarkRenderer._state
    local dt = ImGui.GetDeltaTime()

    state.lastUpdateTimer = state.lastUpdateTimer + dt
    
    if state.lastUpdateTimer >= state.updateRate then
        local r, g, b, a = Client.GetMenuColor()
        local lc = state.lastColor

        if r ~= lc.r or g ~= lc.g or b ~= lc.b or a ~= lc.a then
            COL_Accent = ImGui.Color(r, g, b, a)
        
            local accentIndices = {2, 4, 6, 8, 10, 12}
            for _, idx in ipairs(accentIndices) do
                staticSegments[idx].color = COL_Accent
            end
        
            lc.r, lc.g, lc.b, lc.a = r, g, b, a
        end

        staticSegments[14].text = os_date("%H:%M:%S")
        
        local fps = floor(ImGui.GetFPS())
        staticSegments[2].text = STR_POOL_INT[fps] or tostring(fps)
        
        local pc = SDK.GetLocalPC()
        local myPawn = (pc ~= 0) and PC.GetPawn(pc) or 0
        local ping = (myPawn ~= 0) and floor(Character.GetExactPing(myPawn)) or 0
        staticSegments[4].text = STR_POOL_INT[ping] or tostring(ping)

        local cpu_usage, cpu_freq = System.GetCPUStats()
        local cpu_int = floor(cpu_usage)
        staticSegments[6].text = STR_POOL_PERCENT[cpu_int] or (cpu_int .. "%")
        staticSegments[8].text = format("%.1f", cpu_freq * 0.001)
        
        local gpu_int = floor(System.GetGPUStats())
        staticSegments[10].text = STR_POOL_PERCENT[gpu_int] or (gpu_int .. "%")
        
        if not CHAR_CLASS then CHAR_CLASS = SDK.GetCharacterClass() end
        if not DINO_CLASS then DINO_CLASS = SDK.GetDinoClass() end
        local actors = SDK.GetActors()
        local count = 0
        for i=1, #actors do
            local addr = actors[i]
            if Actor.IsA(addr, CHAR_CLASS) or Actor.IsA(addr, DINO_CLASS) then
                count = count + 1
            end
        end
        staticSegments[12].text = STR_POOL_INT[count] or tostring(count)

        local maxH = 0
        for i=1, #staticSegments do
            local size = ImGui.CalcTextSize(staticSegments[i].text)
            staticSegments[i]._w = size.x
            if size.y > maxH then maxH = size.y end
        end
        state.targetHeight = maxH
        
        state.lastUpdateTimer = 0
    end

    local cfg = WatermarkRenderer.DEFAULT_CONFIG
    local totalW = state.currentWidth + (cfg.fade * 2)
    WatermarkRenderer.DrawMultiColor(staticSegments, screenSize.x - totalW - 5, 5)
end

function OnPaint()
    Main()
end