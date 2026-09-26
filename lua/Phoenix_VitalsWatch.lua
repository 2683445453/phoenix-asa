-- 生存与负重提醒 / Vitals and weight reminders. Read-only, no item actions.
-- Values are ratios: 0.25=25%. Ctrl+F8 mutes/unmutes notifications.
local Settings={health_low=.30,oxygen_low=.25,weight_high=.85,cooldown_seconds=30,mute_key=0x177,x=24,y=232}
local muted,was_down,last_alert,session=false,false,{},nil
local function text(zh,en) return Client.IsEnglish() and en or zh end
function OnPaint()
    local s=Client.GetSessionSnapshot()
    local now=Client.GetTimeMilliseconds()
    local down=Client.IsChordDown(Settings.mute_key)
    if down and not was_down then muted=not muted end
    was_down=down
    if not s.valid then return end
    if session~=s.session then last_alert={};session=s.session end
    local alerts={}
    local function check(key,active,label)
        if not active then return end
        alerts[#alerts+1]=label
        if not muted and s.input_ready and (not last_alert[key] or now-last_alert[key]>=math.max(5,Settings.cooldown_seconds)*1000) then
            Client.AddLog(245,205,140,255,label);last_alert[key]=now
        end
    end
    check("health",s.health>=0 and s.health<=Settings.health_low,text("生命偏低","Low health"))
    check("oxygen",s.oxygen>=0 and s.oxygen<=Settings.oxygen_low,text("氧气偏低","Low oxygen"))
    check("weight",s.weight>=Settings.weight_high,text("负重接近上限","Backpack nearly full"))
    if #alerts==0 then return end
    local label=table.concat(alerts," · ")..(muted and text(" · 提醒静音"," · muted") or "")
    local size=ImGui.CalcTextSize(label)
    ImGui.AddRectFilled(Settings.x,Settings.y,Settings.x+size.x+24,Settings.y+size.y+18,ImGui.Color(28,28,30,210),8)
    ImGui.AddText(Settings.x+12,Settings.y+9,ImGui.Color(245,205,140,245),label)
end
function OnDisconnect() session=nil;last_alert={};was_down=false end
function OnShutDown() OnDisconnect() end
