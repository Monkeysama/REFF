-- REFF 可选示例插件：只修改脚本会话内存，不读取或写入玩家存档。
local native = rawget(_G, "reff_native")

-- 设置示例自行提供旧 ImGui 回退；REFF 核心不感知插件的回退界面。
local function install_settings_fallback()
    local window_open = true
    local enabled = true
    re.on_draw_ui(function()
        if type(imgui) ~= "table" or not window_open then return end
        if imgui.begin_window("REFF 设置示例（回退）", window_open) then
            imgui.text("REFF 不可用，当前使用 REFramework 原生界面")
            local changed
            changed, enabled = imgui.checkbox("启用", enabled)
            if imgui.button("重置设置") then enabled = true end
            imgui.end_window()
        end
    end)
end

if native == nil then
    install_settings_fallback()
    return
end

local SDK = require("reff.sdk")
local interaction = { count = 0, revision = 0 }
local counter = { count = 0, revision = 0 }
local settings = { enabled = true, intensity = 50, note = "", revision = 0 }
local started_at = os.clock()
local interaction_handle
local counter_handle
local settings_handle
local status_handle

-- 原生事件出口只接收序列化 JSON；SDK 在调用前再次校验插件和事件所有权。
local sdk_native = {
    is_ready = native.is_ready,
    emit = function(plugin_id, event_name, payload)
        return native.emit(plugin_id, event_name, json.dump_string(payload))
    end,
}

-- 交互示例提供独立的增减计数器，用于验证隔离页请求、事件与连续点击。
local function get_interaction()
    return { count = interaction.count, revision = interaction.revision }
end
local function change_interaction(params)
    assert(type(params) == "table", "参数必须是对象")
    local amount = params.amount or 1
    assert(type(amount) == "number" and amount == math.floor(amount) and math.abs(amount) <= 10,
        "增量必须是 -10 到 10 的整数")
    interaction.count = interaction.count + amount
    interaction.revision = interaction.revision + 1
    interaction_handle.emit("example.interaction.changed", get_interaction())
    return get_interaction()
end

-- Schema 计数器返回当前值和修订号，Shell 只按 manifest 声明的字段合并快照。
local function get_counter()
    return { count = counter.count, revision = counter.revision }
end
local function increment_counter(params)
    assert(type(params) == "table", "参数必须是对象")
    local amount = params.amount or 1
    assert(type(amount) == "number" and amount == math.floor(amount) and math.abs(amount) <= 10,
        "步进必须是 -10 到 10 的整数")
    counter.count = counter.count + amount
    counter.revision = counter.revision + 1
    counter_handle.emit("example.counter.changed", get_counter())
    return get_counter()
end

-- 设置示例再次校验字段类型和范围，前端校验不作为游戏侧信任依据。
local function get_settings()
    return { enabled = settings.enabled, intensity = settings.intensity, note = settings.note, revision = settings.revision }
end
local function set_settings(params)
    assert(type(params) == "table", "参数必须是对象")
    if params.enabled ~= nil then assert(type(params.enabled) == "boolean", "enabled 必须是布尔值"); settings.enabled = params.enabled end
    if params.intensity ~= nil then
        assert(type(params.intensity) == "number" and params.intensity == math.floor(params.intensity) and
            params.intensity >= 0 and params.intensity <= 100, "intensity 必须是 0 到 100 的整数")
        settings.intensity = params.intensity
    end
    if params.note ~= nil then assert(type(params.note) == "string" and #params.note <= 64, "note 长度不能超过 64 字节"); settings.note = params.note end
    settings.revision = settings.revision + 1
    settings_handle.emit("example.settings.changed", get_settings())
    return get_settings()
end

-- 状态示例只返回低频摘要，不暴露游戏对象、地址或用户数据。
local function get_status()
    return { connected = true, uptime = math.max(0, math.floor(os.clock() - started_at)) }
end

interaction_handle = SDK.register("example.interaction", {
    methods = { ["example.interaction.get"] = get_interaction, ["example.interaction.change"] = change_interaction },
    events = { "example.interaction.changed" },
}, sdk_native)
counter_handle = SDK.register("example.counter", {
    methods = { ["example.counter.get"] = get_counter, ["example.counter.increment"] = increment_counter },
    events = { "example.counter.changed" },
}, sdk_native)
settings_handle = SDK.register("example.settings", {
    methods = { ["example.settings.get"] = get_settings, ["example.settings.set"] = set_settings },
    events = { "example.settings.changed" },
}, sdk_native)
status_handle = SDK.register("example.status", {
    methods = { ["example.status.get"] = get_status },
    events = {},
}, sdk_native)

log.info("REFF: 可选示例插件已注册")
