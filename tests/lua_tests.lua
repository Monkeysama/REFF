-- 在同源 Lua 5.4.3 中验证真实调度模块，编解码依赖在此替换成可观察的测试对象。
local root = assert(arg[1], "source root required")
package.path = root .. "/lua/?.lua;" .. package.path
local Bridge = require("reff.bridge")
local requests, responses, ready, count = {}, {}, false, 0
local native = {
    poll = function() return table.remove(requests, 1) end,
    respond = function(value) responses[#responses + 1] = value end,
    set_ready = function(value) ready = value end,
}
local codec = { load = function(value) return value end, dump = function(value) return value end }
local bridge = Bridge.create(native, codec, function() return 0 end, {
    increment = function(params) assert(type(params.amount) == "number"); count = count + params.amount; return { count = count } end,
})
assert(ready)
requests[1] = { id = "1", epoch = 2, method = "increment", params = { amount = 1 } }
requests[2] = { id = "2", epoch = 2, method = "missing", params = {} }
requests[3] = { id = "3", epoch = 2, method = "increment", params = { amount = "bad" } }
bridge.update()
assert(count == 1 and #responses == 3)
assert(responses[1].result.count == 1 and responses[1].epoch == 2)
assert(responses[2].error.code == "METHOD_NOT_FOUND")
assert(responses[3].error.code == "HANDLER_ERROR")
for i = 1, 40 do requests[i] = { id = tostring(i), method = "increment", params = { amount = 1 } } end
bridge.update()
assert(#requests == 8 and count == 33, "per-tick limit")
bridge.close(); bridge.close(); bridge.update()
assert(not ready and count == 33 and #requests == 8, "closed bridge cannot execute")
-- M3 SDK 注册、就绪查询和幂等注销边界。
local SDK = require("reff.sdk")
local emitted = {}
local sdk_native = {
    is_ready = function() return true end,
    emit = function(plugin_id, event_name, payload) emitted[#emitted + 1] = { plugin_id, event_name, payload }; return true end,
}
local handle, methods = SDK.register("example.sdk", { methods = { ["example.sdk.get"] = function() return { ok = true } end } }, sdk_native)
assert(methods["example.sdk.get"] and handle.is_ui_ready())
assert((SDK._handler("example.sdk", "example.sdk.get"))().ok)
handle.unregister(); handle.unregister()
assert(SDK._handler("example.sdk", "example.sdk.get") == nil)
local first = SDK.register("example.first", {
    methods = { ["example.first.get"] = function() return { first = true } end },
    events = { "example.first.changed" },
}, sdk_native)
local second = SDK.register("example.second", {
    methods = { ["example.second.get"] = function() return { second = true } end },
}, sdk_native)
assert(SDK._handler("example.first.get") and SDK._handler("example.second.get"), "multi-plugin method registry")
local subscribed, owner = SDK._subscribe("example.first.changed", "example.first.changed:test")
assert(subscribed and owner == "example.first", "event subscription registry")
assert(not SDK._subscribe("example.first.changed", "example.second.changed:test", "example.second"), "cross-plugin event rejected")
assert(SDK._handler("example.second", "example.first.get") == nil, "cross-plugin method rejected")
assert(not SDK._subscribe("example.second.missing", "example.second.missing:test"), "unknown event rejected")
assert(first.emit("example.first.changed", { revision = 1 }) and #emitted == 1, "event emit")
assert(SDK._unsubscribe("example.first.changed:test") and not SDK._unsubscribe("example.first.changed:test"), "idempotent unsubscribe")
first.unregister(); second.unregister(); SDK._clear()
print("Lua bridge checks passed")
