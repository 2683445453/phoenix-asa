-- 活动计时器 / Session timer. Edit Settings, then reload this script.
-- Chords: Ctrl=0x100, Alt=0x200, Shift=0x400, plus a Windows virtual key.
local Settings = { pause_key=0x175, reset_key=0x575, reminder_minutes=30, x=24, y=140 }
local elapsed, last, paused, last_reminder = 0, nil, false, 0
local keys = {}
local function text(zh,en) return Client.IsEnglish() and en or zh end
local function pressed(key)
    local down=Client.IsChordDown(key)
    local edge=down and not keys[key]; keys[key]=down; return edge
end
function OnPaint()
    local now=Client.GetTimeMilliseconds()
    local s=Client.GetSessionSnapshot()
    local dt=last and math.max(0,math.min(1000,now-last)) or 0; last=now
    if pressed(Settings.pause_key) then paused=not paused end
    if pressed(Settings.reset_key) then elapsed=0; last_reminder=0 end
    if s.valid and s.input_ready and not paused then elapsed=elapsed+dt end
    local interval=math.max(1,Settings.reminder_minutes)*60000
    if elapsed-last_reminder>=interval then
        last_reminder=elapsed
        Client.AddLog(190,214,235,255,text("活动计时提醒：可以休息一下。","Session reminder: time for a short break."))
    end
    if not s.valid then return end
    local seconds=math.floor(elapsed/1000)
    local label=string.format("%s %02d:%02d:%02d%s",text("活动计时","Session"),math.floor(seconds/3600),math.floor(seconds/60)%60,seconds%60,
        paused and text(" · 暂停"," · paused") or (not s.input_ready and text(" · 等待游戏"," · waiting") or ""))
    local size=ImGui.CalcTextSize(label)
    ImGui.AddRectFilled(Settings.x,Settings.y,Settings.x+size.x+24,Settings.y+size.y+18,ImGui.Color(28,28,30,210),8)
    ImGui.AddText(Settings.x+12,Settings.y+9,ImGui.Color(238,238,240,245),label)
end
function OnDisconnect() last=nil; keys={} end
function OnShutDown() last=nil; keys={} end
