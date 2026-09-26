-- 返回标记 / Return waypoint. No movement, teleportation or server commands.
-- Ctrl+F7 marks this position; Ctrl+Shift+F7 clears it. Edit keys below.
local Settings={mark_key=0x176,clear_key=0x576,x=24,y=186}
local point, session, keys=nil,nil,{}
local function text(zh,en) return Client.IsEnglish() and en or zh end
local function pressed(key)
    local down=Client.IsChordDown(key)
    local edge=down and not keys[key]; keys[key]=down; return edge
end
function OnPaint()
    local s=Client.GetSessionSnapshot()
    if not s.valid then return end
    if session~=s.session then point=nil;session=s.session end
    if pressed(Settings.mark_key) then
        point={x=s.x,y=s.y,z=s.z}
        Client.AddLog(190,214,235,255,text("已记录返回位置。","Return point recorded."))
    end
    if pressed(Settings.clear_key) then point=nil end
    if not point then return end
    local dx,dy,dz=point.x-s.x,point.y-s.y,point.z-s.z
    local distance=math.sqrt(dx*dx+dy*dy+dz*dz)
    local label=string.format(text("返回点 %.0f m · X %+.0f · Y %+.0f · 高差 %+.0f m","Return %.0f m · X %+.0f · Y %+.0f · Height %+.0f m"),distance,dx,dy,dz)
    local size=ImGui.CalcTextSize(label)
    ImGui.AddRectFilled(Settings.x,Settings.y,Settings.x+size.x+24,Settings.y+size.y+18,ImGui.Color(28,28,30,210),8)
    ImGui.AddText(Settings.x+12,Settings.y+9,ImGui.Color(238,238,240,245),label)
end
function OnDisconnect() point=nil;session=nil;keys={} end
function OnShutDown() OnDisconnect() end
