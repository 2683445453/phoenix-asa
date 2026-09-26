local function fixture(path)
    local s={valid=false,input_ready=true,session=1,x=0,y=0,z=0,health=1,oxygen=1,weight=.2}
    local f={time=0,keys={},labels={},logs={},s=s}
    local env=setmetatable({Client={},ImGui={}},{__index=_G})
    env.Client.GetTimeMilliseconds=function()return f.time end
    env.Client.IsEnglish=function()return true end
    env.Client.GetSessionSnapshot=function()return s end
    env.Client.IsChordDown=function(k)return s.input_ready and f.keys[k] or false end
    env.Client.AddLog=function(r,g,b,a,t)f.logs[#f.logs+1]=t end
    env.ImGui.Color=function()return 0 end
    env.ImGui.CalcTextSize=function(t)return {x=#t*7,y=18} end
    env.ImGui.AddRectFilled=function()end
    env.ImGui.AddText=function(x,y,c,t)f.labels[#f.labels+1]=t end
    local chunk=assert(loadfile(path));setfenv(chunk,env);chunk()
    f.env=env
    function f:paint(ms) self.time=self.time+(ms or 100);self.labels={};env.OnPaint() end
    f:paint();assert(#f.labels==0) -- menus/missing survivor never show stale data
    s.valid=true
    return f
end
local t=fixture('lua/Phoenix_SessionTimer.lua')
t:paint(1000);assert(t.labels[1]:find('00:00:01',1,true))
t.keys[0x175]=true;t:paint(1000);assert(t.labels[1]:find('paused',1,true))
t:paint(1000);assert(t.labels[1]:find('paused',1,true)) -- long hold toggles once
t.keys[0x175]=false;t:paint();t.keys[0x175]=true;t:paint(1000);assert(not t.labels[1]:find('paused',1,true))
t.keys[0x175]=false;t.s.input_ready=false;local paused=t.labels[1]:match('%d%d:%d%d:%d%d');t:paint(1000);assert(t.labels[1]:find(paused,1,true))
t.s.input_ready=true;t.keys[0x575]=true;t:paint(0);assert(t.labels[1]:find('00:00:00',1,true));t.keys[0x575]=false
for i=1,1800 do t:paint(1000) end
assert(#t.logs==1);t:paint();assert(#t.logs==1)
local w=fixture('lua/Phoenix_ReturnWaypoint.lua')
w.keys[0x176]=true;w:paint();assert(#w.logs==1);w:paint();assert(#w.logs==1)
w.keys[0x176]=false;w.s.x=3;w.s.y=4;w:paint();assert(w.labels[1]:find('Return 5 m',1,true))
w.s.session=2;w:paint();assert(#w.labels==0)
w.keys[0x176]=true;w:paint();w.keys[0x176]=false;w.keys[0x576]=true;w:paint();assert(#w.labels==0)
w.env.OnDisconnect();w:paint();assert(#w.labels==0)
local v=fixture('lua/Phoenix_VitalsWatch.lua')
v:paint();assert(#v.labels==0 and #v.logs==0)
v.s.health=.1;v.s.oxygen=.1;v.s.weight=.95;v:paint();assert(#v.logs==3 and #v.labels==1)
v:paint(1000);assert(#v.logs==3);v:paint(30000);assert(#v.logs==6)
v.keys[0x177]=true;v:paint();v:paint(30000);assert(#v.logs==6 and v.labels[1]:find('muted',1,true))
v.keys[0x177]=false;v.s.health=-1;v.s.oxygen=-1;v.s.weight=-1;v:paint();assert(#v.labels==0)
v.env.OnShutDown();t.env.OnShutDown();w.env.OnShutDown()
print('Lua timer pause/reset/reminders, waypoint distance/session reset, vitals mute/debounce and invalid-state handling passed.')
