-- ----------------------------------------------------------------------------
-- 🎨 Indicators 核心系统 (性能极致优化版)
-- ----------------------------------------------------------------------------
local IndicatorSystem = {
    pool = {},
    activeKeys = {},      -- 记录本帧活跃 Key (顺序)
    activeMap = {},       -- 辅助哈希表，用于 O(1) 查找活跃状态
    renderList = {},      -- 预分配渲染列表，减少 GC
    
    -- 配置
    baseX = 20,
    baseY = ImGui.GetScreenSize().y * 0.5, 
    defaultSpacing = 35,
    speed = 10.0,
    lingerTime = 1.0
}

-- 缓存常用数学函数
local m_exp = math.exp
local m_floor = math.floor
local m_sin = math.sin
local m_min = math.min
local m_max = math.max
local m_sqrt = math.sqrt
local m_abs = math.abs

local function SmoothTo(current, target, speed, dt)
    return current + (target - current) * (1 - m_exp(-speed * dt))
end

-- ----------------------------------------------------------------------------
-- 封装接口
-- ----------------------------------------------------------------------------
Indicators = {}

function Indicators.Add(key, r, g, b, a, text)
    if not key or not text then return end
    
    local now = os.clock()
    
    -- 记录活跃状态
    if not IndicatorSystem.activeMap[key] then
        table.insert(IndicatorSystem.activeKeys, key)
        IndicatorSystem.activeMap[key] = true
    end
    
    local s = IndicatorSystem.pool[key]
    if not s then
        s = {
            currY = IndicatorSystem.baseY, 
            currA = 0,
            currH = 0,
            text = text,
            r = r, g = g, b = b,
            lastActive = now,
            isActive = true
        }
        IndicatorSystem.pool[key] = s
    else
        s.text = text
        s.r, s.g, s.b = r, g, b
        s.lastActive = now
        s.isActive = true
    end
end

function Indicators.EndFrame()
    local dt = ImGui.GetDeltaTime()
    local now = os.clock()
    local sys = IndicatorSystem
    
    -- 1. 更新状态与计算目标 (合并循环)
    for key, s in pairs(sys.pool) do
        -- 更新 isActive 状态 (如果本帧没被 Add)
        if not sys.activeMap[key] then 
            s.isActive = false 
        end

        local targetA, targetH
        if s.isActive then
            targetA = 255
            targetH = sys.defaultSpacing
        else
            if (now - s.lastActive) > sys.lingerTime then
                targetA = 0
                targetH = 0
            else
                targetA = 0 
                targetH = sys.defaultSpacing 
            end
        end

        s.currA = SmoothTo(s.currA, targetA, sys.speed, dt)
        s.currH = SmoothTo(s.currH, targetH, sys.speed, dt)
    end

    -- 2. 构建渲染列表 (优化查重效率)
    local renderList = sys.renderList
    local renderIdx = 1
    
    -- 活跃的先入列
    for i = 1, #sys.activeKeys do
        local key = sys.activeKeys[i]
        renderList[renderIdx] = key
        renderIdx = renderIdx + 1
    end
    
    -- 非活跃但还在淡出的补充入列
    for key, s in pairs(sys.pool) do
        if not sys.activeMap[key] then
            renderList[renderIdx] = key
            renderIdx = renderIdx + 1
        end
    end

    -- 3. 排序后的渲染与清理
    local accumulatedY = sys.baseY
    for i = 1, renderIdx - 1 do
        local key = renderList[i]
        local s = sys.pool[key]
        
        if s then
            s.currY = SmoothTo(s.currY, accumulatedY, sys.speed, dt)
            
            if s.currA > 1 then
                DrawStylishTextRect(sys.baseX, s.currY, s.r, s.g, s.b, s.currA, s.text)
            end
            
            accumulatedY = accumulatedY - s.currH

            -- 清理失效对象
            if not s.isActive and s.currA < 1 and s.currH < 1 then
                sys.pool[key] = nil
            end
        end
        renderList[i] = nil -- 清理引用
    end
    
    -- 重置临时辅助容器
    for i = 1, #sys.activeKeys do
        sys.activeMap[sys.activeKeys[i]] = nil
        sys.activeKeys[i] = nil
    end
end

-- ----------------------------------------------------------------------------
-- ✍️ 基础渲染函数 (减少计算开销)
-- ----------------------------------------------------------------------------
function DrawStylishTextRect(x, y, r, g, b, a, text)
    if a < 1 then return end
    
    local textSize = ImGui.CalcTextSize(text)
    local tw, th = textSize.x, textSize.y
    local paddingV = 4
    local rectH = th + 8 -- 4*2
    
    local ix, iy = m_floor(x), m_floor(y)
    local ir, ig, ib, ia = m_floor(r), m_floor(g), m_floor(b), m_floor(a)
    
    -- 侧边淡入宽度计算优化
    local sideFadeW = (tw / m_max(#text, 1)) * 2
    local x1 = ix + m_floor(sideFadeW)
    local x2 = x1 + m_floor(tw)
    local x3 = x2 + m_floor(sideFadeW)
    
    local colS = ImGui.Color(0, 0, 0, m_floor(ia * 0.2))
    local colT = ImGui.Color(0, 0, 0, 0)
    local colText = ImGui.Color(ir, ig, ib, ia)
    local colShadow = ImGui.Color(0, 0, 0, ia)
    
    -- 绘制背景
    ImGui.AddRectFilledMultiColor(ix, iy, x1, iy+rectH, colT, colS, colS, colT)
    ImGui.AddRectFilled(x1, iy, x2, iy+rectH, colS, 0)
    ImGui.AddRectFilledMultiColor(x2, iy, x3, iy+rectH, colS, colT, colT, colS)
    
    -- 绘制文字 (投影 + 主体)
    ImGui.AddText(x1 + 1, iy + paddingV + 1, colShadow, text)
    ImGui.AddText(x1, iy + paddingV, colText, text)
end

-- ==========================================
-- FPS & PING 波动监测系统 (优化版)
-- ==========================================

local fps_history = {}
local ping_history = {}
local warning_state = {
    FPS = { active = false, start_time = 0, current_color = {r = 202, g = 255, b = 112} },
    PING = { active = false, start_time = 0, current_color = {r = 202, g = 255, b = 112} }
}

-- 静态配置
local COLOR_ORANGE_RED = {r = 255, g = 99, b = 71}
local COLOR_KHAKI      = {r = 255, g = 236, b = 139}
local COLOR_OLIVE      = {r = 202, g = 255, b = 112}

local SENSITIVITY = 0.15
local SEVERE_THRESHOLD = 0.4
local MIN_WARNING_TIME = 10.0
local TRANSITION_SPEED = 2.5

local function lerp(a, b, t)
    return a + (b - a) * t
end

local function GetCurrentPing()
    local pc = SDK.GetLocalPC()
    if pc ~= 0 then
        local myPawn = PC.GetPawn(pc)
        if myPawn ~= 0 then
            return Character.GetExactPing(myPawn)
        end
    end
    return 0
end

local function get_volatility_ratio(data_table)
    local count = #data_table
    if count < 10 then return 0 end
    
    local sum = 0
    for i = 1, count do sum = sum + data_table[i] end
    local avg = sum / count
    if avg < 0.001 then return 0 end

    local square_diff_sum = 0
    for i = 1, count do 
        local diff = data_table[i] - avg
        square_diff_sum = square_diff_sum + (diff * diff)
    end
    local std_dev = m_sqrt(square_diff_sum / count)
    return std_dev / avg
end

local function UpdateMonitor(key, current_val, dt, current_time, history_table)
    local state = warning_state[key]
    
    -- 维护采样窗口
    table.insert(history_table, current_val)
    local max_samples = m_floor(5.0 / m_max(dt, 0.001))
    while #history_table > max_samples do
        table.remove(history_table, 1)
    end

    local vol_ratio = get_volatility_ratio(history_table)
    local is_currently_fluctuating = vol_ratio > SENSITIVITY
    
    if is_currently_fluctuating and not state.active then
        state.active = true
        state.start_time = current_time
    end

    local target_color = COLOR_OLIVE
    if state.active then
        if vol_ratio > SEVERE_THRESHOLD then
            target_color = COLOR_ORANGE_RED
        elseif is_currently_fluctuating or (current_time - state.start_time < MIN_WARNING_TIME) then
            -- 呼吸效果计算
            local wave = (m_sin(current_time * 4) + 1) * 0.5
            target_color = {
                r = lerp(COLOR_OLIVE.r, COLOR_KHAKI.r, wave),
                g = lerp(COLOR_OLIVE.g, COLOR_KHAKI.g, wave),
                b = lerp(COLOR_OLIVE.b, COLOR_KHAKI.b, wave)
            }
        else
            target_color = COLOR_OLIVE
            if m_abs(state.current_color.g - COLOR_OLIVE.g) < 0.5 then
                state.active = false
            end
        end
    end

    -- 颜色平滑
    local t = m_max(0, m_min(dt * TRANSITION_SPEED, 1.0))
    state.current_color.r = lerp(state.current_color.r, target_color.r, t)
    state.current_color.g = lerp(state.current_color.g, target_color.g, t)
    state.current_color.b = lerp(state.current_color.b, target_color.b, t)

    if state.active then
        Indicators.Add(key, 
            m_floor(state.current_color.r), 
            m_floor(state.current_color.g), 
            m_floor(state.current_color.b), 
            255, key)
    end
end

local function TickMonitor()
    local dt = ImGui.GetDeltaTime()
    local current_time = os.clock()
    UpdateMonitor("FPS", ImGui.GetFPS(), dt, current_time, fps_history)
    UpdateMonitor("PING", GetCurrentPing(), dt, current_time, ping_history)
end

-- ----------------------------------------------------------------------------
-- 🎮 主入口
-- ----------------------------------------------------------------------------
function OnPaint()
    local pc = SDK.GetLocalPC()
    if pc ~= 0 then
        local myPawn = PC.GetPawn(pc)
        if myPawn ~= 0 then
            TickMonitor()
            Indicators.EndFrame()
        end
    end
end