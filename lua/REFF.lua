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

-- UpdateBehavior 是当前首版的游戏线程调度点；handler 不得在这里执行长任务。
re.on_application_entry("UpdateBehavior", function()
    bridge.update()
end)

-- Reset Scripts 使全部旧 handler、订阅和请求失效，随后由各插件脚本重新注册。
re.on_script_reset(function()
    bridge.close()
    SDK._clear()
end)

log.info("REFF: Lua 桥接已就绪，按 F8 打开网页面板")
