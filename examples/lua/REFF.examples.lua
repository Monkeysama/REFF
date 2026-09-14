-- REFF 可选示例插件：只修改脚本会话内存，不读取或写入玩家存档。
local native = rawget(_G, "reff_native")

if native == nil then
    -- 没有 REFF 原生桥接时不注册示例服务，避免与 REFramework 原生 UI 产生竞争。
    return
end

local SDK = require("reff.sdk")
local refresh_count = 0
local started_at = os.clock()
local vue_handle
local react_handle
local html_handle
local sdk_api = rawget(_G, "sdk")
local hp_reflection_cache = {}

-- 原生事件出口只接收序列化 JSON；SDK 在调用前再次校验插件和事件所有权。
local sdk_native = {
    is_ready = native.is_ready,
    emit = function(plugin_id, event_name, payload)
        return native.emit(plugin_id, event_name, json.dump_string(payload))
    end,
}

-- 获取 Wilds 本地玩家角色；只读 PlayerManager 主玩家，不扫描或修改其他角色。
local function get_local_player_character()
    if not sdk_api or not sdk_api.get_managed_singleton then return nil end
    local ok, manager = pcall(sdk_api.get_managed_singleton, "app.PlayerManager")
    if not ok or not manager then return nil end
    local player_ok, player = pcall(function() return manager:call("getMasterPlayer") end)
    if not player_ok or not player then return nil end
    local character_ok, character = pcall(function() return player:call("get_Character") end)
    if character_ok and character then return character end
    return nil
end

-- 按差分管理器兼容顺序解析血量属性，并缓存类型级反射结果。
local function get_hp_reflection(character)
    if not character then return nil end
    local type_ok, type_def = pcall(function() return character:get_type_definition() end)
    if not type_ok or not type_def then return nil end
    local key = tostring(type_def)
    if hp_reflection_cache[key] ~= nil then return hp_reflection_cache[key] or nil end
    local function direct(get_name, max_name, set_name)
        local get_method = type_def:get_method(get_name)
        local max_method = type_def:get_method(max_name)
        if not get_method or not max_method then return nil end
        return { get = get_method, get_max = max_method, set = type_def:get_method(set_name) }
    end
    local reflection = direct("get_HitPoint", "get_MaxHitPoint", "set_HitPoint")
        or direct("get_Health", "get_MaxHealth", "set_Health")
    if not reflection then
        local hunter_method = type_def:get_method("get_HunterHealth")
        local health_type = hunter_method and hunter_method:get_return_type()
        local manager_method = health_type and health_type:get_method("get_HealthMgr")
        local manager_type = manager_method and manager_method:get_return_type()
        local get_method = manager_type and manager_type:get_method("get_Health")
        local max_method = manager_type and manager_type:get_method("get_MaxHealth")
        local set_method = manager_type and manager_type:get_method("set_Health")
        if hunter_method and manager_method and get_method and max_method then
            reflection = { hunter = hunter_method, manager = manager_method, get = get_method, get_max = max_method, set = set_method }
        end
    end
    hp_reflection_cache[key] = reflection or false
    return reflection
end

-- 调用已解析的血量反射；失败时返回 nil，页面显示为暂不可用。
local function read_player_hp()
    local character = get_local_player_character()
    local reflection = get_hp_reflection(character)
    if not character or not reflection then return nil end
    local ok, current, maximum = pcall(function()
        if reflection.hunter then
            local hunter_health = reflection.hunter:call(character)
            local health_manager = reflection.manager:call(hunter_health)
            return reflection.get:call(health_manager), reflection.get_max:call(health_manager)
        end
        return reflection.get:call(character), reflection.get_max:call(character)
    end)
    if not ok or type(current) ~= "number" or type(maximum) ~= "number" or maximum <= 0 then return nil end
    return { current = current, max = maximum, percent = math.max(0, math.min(100, current / maximum * 100)), adjustable = reflection.set ~= nil, character = character, reflection = reflection }
end

-- 设置本地玩家血量百分比；仅允许 Wilds 且限制在 0 到 100 之间。
local function set_player_hp_percent(percent)
    local game_name = reframework:get_game_name()
    local normalized_game = string.lower(tostring(game_name or "")):gsub("[%s_%-]", "")
    if normalized_game ~= "mhwilds" and not normalized_game:find("monsterhunterwilds", 1, true) then
        return false, "当前游戏不是 Monster Hunter Wilds"
    end
    local numeric = tonumber(percent)
    if not numeric then return false, "血量百分比必须是数字" end
    numeric = math.max(0, math.min(100, numeric))
    local state = read_player_hp()
    if not state or not state.adjustable then return false, "当前游戏或角色不支持调整血量" end
    local ok = pcall(function()
        local target = state.max * numeric / 100
        if state.reflection.hunter then
            local hunter_health = state.reflection.hunter:call(state.character)
            local health_manager = state.reflection.manager:call(hunter_health)
            state.reflection.set:call(health_manager, target)
        else
            state.reflection.set:call(state.character, target)
        end
    end)
    if not ok then return false, "调整玩家血量失败" end
    return true
end

-- 三个前端示例共享同一份安全摘要，演示调用 REFramework 与 REFF 运行时信息。
local function get_snapshot()
    local game_name = reframework:get_game_name()
    local hp = nil
    local normalized_game = string.lower(tostring(game_name or "")):gsub("[%s_%-]", "")
    if normalized_game == "mhwilds" or normalized_game:find("monsterhunterwilds", 1, true) then
        local state = read_player_hp()
        if state then hp = { available = true, current = state.current, max = state.max, percent = state.percent, adjustable = state.adjustable } end
    end
    return {
        gameName = game_name,
        reframeworkVersion = reframework:get_version_string(),
        hp = hp or { available = false, adjustable = false },
        uptimeSeconds = math.max(0, math.floor(os.clock() - started_at)),
        refreshCount = refresh_count,
    }
end
local function make_refresh(handle, event_name)
    return function()
        refresh_count = refresh_count + 1
        local value = get_snapshot()
        if handle and event_name then handle.emit(event_name, value) end
        return value
    end
end

local vue_refresh = function()
    refresh_count = refresh_count + 1
    local value = get_snapshot()
    if vue_handle then vue_handle.emit("example.vue.refreshed", value) end
    return value
end
local function make_set_hp()
    return function(params)
        local ok, error_message = set_player_hp_percent(type(params) == "table" and params.percent or nil)
        if not ok then error(error_message) end
        return get_snapshot()
    end
end
vue_handle = SDK.register("example.vue", { methods = { ["example.vue.get"] = get_snapshot, ["example.vue.refresh"] = vue_refresh, ["example.vue.set-hp"] = make_set_hp() }, events = { "example.vue.refreshed" }, }, sdk_native)
react_handle = SDK.register("example.react", { methods = { ["example.react.get"] = get_snapshot, ["example.react.refresh"] = make_refresh(), ["example.react.set-hp"] = make_set_hp() }, events = {} }, sdk_native)
html_handle = SDK.register("example.html", { methods = { ["example.html.get"] = get_snapshot, ["example.html.refresh"] = make_refresh(), ["example.html.set-hp"] = make_set_hp() }, events = {} }, sdk_native)

log.info("REFF: 可选示例插件已注册")
