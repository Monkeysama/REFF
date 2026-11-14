-- REFF 核心入口：只管理桥接、调度和插件注册表，不持有任何示例或业务插件状态。
local native = rawget(_G, "reff_native")

if native == nil then
    log.warn("REFF: 原生插件未加载，跳过网页桥接")
    return
end

local Bridge = require("reff.bridge")
local SDK = require("reff.sdk")

-- 订阅请求在游戏线程交给 SDK 注册表校验；网页不能自行指定事件所有者。
local function subscribe(params, request)
    assert(type(params) == "table" and type(params.eventName) == "string" and type(params.subscriptionId) == "string",
        "订阅参数必须是对象")
    local requester_plugin_id = type(request) == "table" and request.pluginId or nil
    local ok, result = SDK._subscribe(params.eventName, params.subscriptionId, requester_plugin_id)
    assert(ok, result)
    return { subscriptionId = params.subscriptionId, pluginId = result, eventName = params.eventName }
end

-- 取消订阅是幂等清理路径；未知句柄不会影响其他插件的注册状态。
local function unsubscribe(params)
    assert(type(params) == "table" and type(params.subscriptionId) == "string", "取消订阅参数必须是对象")
    SDK._unsubscribe(params.subscriptionId)
    return { subscriptionId = params.subscriptionId }
end

-- Bridge 只解析通用控制方法和 SDK 已注册 handler，具体插件逻辑由独立脚本提供。
local bridge = Bridge.create(native, { load = json.load_string, dump = json.dump_string }, os.clock, {
    resolve = function(method, request)
        if method == "ui.subscribe" then return subscribe end
        if method == "ui.unsubscribe" then return unsubscribe end
        return SDK._handler(request.pluginId, method)
    end,
})

-- 游戏线程上的协作登记只拥有 REFF 槽位，不接管引擎鼠标模式、Hook 或其他插件租约。
-- 新原生接口包含关闭面板后的待释放输入；旧 DLL 回退到显隐查询，仅作尽力兼容。
local blocker_key = "__AVM_UI_INPUT_BLOCKER_STATES"
local blocker_table, blocker_owned, blocker_closed = nil, false, false
local blocker_warnings = {}
local function warn_blocker_once(reason)
    if blocker_warnings[reason] then return end
    blocker_warnings[reason] = true
    log.warn("REFF: 输入协作登记跳过：" .. reason)
end

-- 仅撤销本实例写入的布尔 true；保留共享表及其他插件的数据，不覆盖后来被更改的值。
local function release_blocker()
    if blocker_owned and rawget(blocker_table, "REFF") == true then rawset(blocker_table, "REFF", nil) end
    blocker_table, blocker_owned = nil, false
end

local function update_blocker()
    if blocker_closed then return end
    local query = native.is_input_blocking or native.is_visible
    if type(query) ~= "function" then return end
    local ok, blocked = pcall(query)
    if not ok or type(blocked) ~= "boolean" then
        warn_blocker_once("原生输入状态不可读")
        return -- 查询失败不能擅自撤销已有的输入保护。
    end
    if not blocked then release_blocker(); return end
    local states = rawget(_G, blocker_key)
    if states == nil then states = {}; rawset(_G, blocker_key, states) end
    if type(states) ~= "table" then warn_blocker_once("共享状态不是 table"); return end
    if blocker_table ~= states then release_blocker() end
    local previous = rawget(states, "REFF")
    if previous ~= nil and type(previous) ~= "boolean" then
        warn_blocker_once("REFF 槽位不是 boolean")
        return
    end
    rawset(states, "REFF", true)
    blocker_table, blocker_owned = states, true
end

-- UpdateBehavior 是游戏线程调度点；先同步保护租约，handler 不得在这里执行长任务。
re.on_application_entry("UpdateBehavior", function()
    update_blocker()
    bridge.update()
end)

-- Reset Scripts 使全部旧 handler、订阅和请求失效，随后由各插件脚本重新注册。
re.on_script_reset(function()
    blocker_closed = true
    release_blocker()
    bridge.close()
    SDK._clear()
end)

log.info("REFF: Lua 桥接已就绪，使用 REFF 设置中的快捷键打开网页面板")
