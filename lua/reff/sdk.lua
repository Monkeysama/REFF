-- M3 Lua SDK：把插件方法、事件和生命周期绑定到当前 REFF 会话，不覆盖 REFramework 原有命名空间。
local SDK = {}
local registry = {}
local methods = {}
local events = {}
local subscriptions = {}

local function valid_id(value)
    if type(value) ~= "string" or #value == 0 or #value > 96 or value:match("[^a-z0-9%.%-]") then return false end
    if value:sub(1, 1):match("[%.%-]") or value:sub(-1):match("[%.%-]") or value:find("%.%.") or value:find("%-%-") then return false end
    return true
end

-- 事件/方法必须使用稳定的小写命名；插件 ID 由 manifest 负责与页面绑定。
local function valid_name(value)
    return valid_id(value)
end

local function add_declared_events(plugin_id, declared)
    local result = {}
    if declared == nil then return result end
    assert(type(declared) == "table", "REFF events 必须是 table")
    for _, name in ipairs(declared) do
        assert(valid_name(name), "REFF event 名称不合法")
        assert(events[name] == nil, "REFF event 已被其他插件声明")
        result[name] = true
    end
    return result
end

-- 注册插件 handler；同一 Lua epoch 内拒绝重复 ID 和重复方法，返回幂等句柄。
function SDK.register(plugin_id, handlers, native)
    assert(valid_id(plugin_id), "REFF plugin id 不合法")
    assert(type(handlers) == "table", "REFF handlers 必须是 table")
    assert(registry[plugin_id] == nil, "REFF plugin id 已注册")
    local plugin_methods = {}
    for name, handler in pairs(handlers.methods or {}) do
        assert(type(name) == "string" and valid_name(name) and type(handler) == "function", "REFF method handler 不合法")
        assert(name:sub(1, #plugin_id + 1) == plugin_id .. ".", "REFF method 必须使用插件命名空间")
        assert(methods[name] == nil, "REFF method 已被其他插件注册")
        plugin_methods[name] = handler
    end
    local plugin_events = add_declared_events(plugin_id, handlers.events)
    local active = true
    local handle = {}
    registry[plugin_id] = { methods = plugin_methods, events = plugin_events, native = native, handle = handle }
    for name, handler in pairs(plugin_methods) do methods[name] = { plugin_id = plugin_id, handler = handler } end
    for name in pairs(plugin_events) do events[name] = plugin_id end
    function handle.is_ui_ready()
        return active and native ~= nil and native.is_ready and native.is_ready()
    end
    function handle.emit(event_name, payload)
        assert(active, "REFF plugin 已注销")
        assert(type(event_name) == "string" and plugin_events[event_name] and type(payload) == "table", "REFF event 参数不合法")
        if native and native.emit then return native.emit(plugin_id, event_name, payload) end
        return false
    end
    function handle.unregister()
        if not active then return end
        active = false
        for name in pairs(plugin_methods) do methods[name] = nil end
        for name in pairs(plugin_events) do
            events[name] = nil
            for subscription_id, value in pairs(subscriptions) do
                if value.event_name == name then subscriptions[subscription_id] = nil end
            end
        end
        if registry[plugin_id] and registry[plugin_id].handle == handle then registry[plugin_id] = nil end
    end
    return handle, methods
end

-- 仅供 Bridge 在游戏线程查询；网页无法直接取得 handler 或 Lua 状态。
function SDK._handler(plugin_id, method)
    -- 兼容早期测试调用 SDK._handler(pluginId, method)，正式 Bridge 使用单参数方法名。
    if method == nil then method = plugin_id; plugin_id = nil end
    local item = methods[method]
    if plugin_id and item and item.plugin_id ~= plugin_id then return nil end
    return item and item.handler or nil, item and item.plugin_id or nil
end

-- 订阅由 Lua 注册表确认事件声明；宿主只保存已确认的订阅句柄。
function SDK._subscribe(event_name, subscription_id, requester_plugin_id)
    if not valid_name(event_name) or type(subscription_id) ~= "string" or #subscription_id == 0 or #subscription_id > 160 then
        return false, "订阅参数不合法"
    end
    local plugin_id = events[event_name]
    if not plugin_id or subscriptions[subscription_id] then return false, "事件未注册或订阅 ID 重复" end
    if requester_plugin_id ~= nil and requester_plugin_id ~= plugin_id then return false, "插件无权订阅该事件" end
    subscriptions[subscription_id] = { plugin_id = plugin_id, event_name = event_name }
    return true, plugin_id
end

function SDK._unsubscribe(subscription_id)
    if type(subscription_id) ~= "string" then return false end
    if not subscriptions[subscription_id] then return false end
    subscriptions[subscription_id] = nil
    return true
end

function SDK._clear()
    local handles = {}
    for _, item in pairs(registry) do handles[#handles + 1] = item.handle end
    for _, handle in ipairs(handles) do if handle and handle.unregister then handle.unregister() end end
    registry = {}
    methods = {}
    events = {}
    subscriptions = {}
end

return SDK
