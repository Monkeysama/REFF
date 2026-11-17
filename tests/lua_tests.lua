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
-- 加载真实核心入口验证协作槽位生命周期；只有本进程测试全局，绝不调用游戏 SDK。
local global_names = { "reff_native", "json", "re", "log", "__AVM_UI_INPUT_BLOCKER_STATES" }
local saved_globals = {}
for _, name in ipairs(global_names) do saved_globals[name] = { value = rawget(_G, name) } end
local blocker_ok, blocker_error = pcall(function()
    local blocked, fail_query, bad_query = false, false, false
    local warnings, callbacks = {}, {}
    local test_native = {
        poll = function() return nil end,
        set_ready = function(value) ready = value end,
        is_input_blocking = function()
            if fail_query then error("test state unavailable") end
            if bad_query then return "invalid" end
            return blocked
        end,
    }
    rawset(_G, "reff_native", test_native)
    rawset(_G, "json", { load_string = function(v) return v end, dump_string = function(v) return v end })
    rawset(_G, "log", { info = function() end, warn = function(v) warnings[#warnings + 1] = v end })
    rawset(_G, "re", {
        on_application_entry = function(name, callback) assert(name == "UpdateBehavior"); callbacks.update = callback end,
        on_script_reset = function(callback) callbacks.reset = callback end,
    })
    rawset(_G, "__AVM_UI_INPUT_BLOCKER_STATES", nil)
    dofile(root .. "/lua/REFF.lua")
    callbacks.update()
    assert(rawget(_G, "__AVM_UI_INPUT_BLOCKER_STATES") == nil, "inactive REFF must not allocate a shared table")
    blocked = true; callbacks.update()
    local states = rawget(_G, "__AVM_UI_INPUT_BLOCKER_STATES")
    assert(states.REFF == true, "visible or pending-release REFF registers its lease")
    states.ArmorVariantManager, states.ShapeKeySystem = true, true
    callbacks.update(); assert(states.REFF == true, "held-input drain retains lease")
    fail_query = true; callbacks.update(); callbacks.update()
    assert(states.REFF == true and #warnings == 1, "failed query must retain lease and warn once")
    fail_query = false; bad_query = true; callbacks.update()
    assert(states.REFF == true and #warnings == 1, "invalid query must not release lease")
    bad_query = false; blocked = false; callbacks.update()
    assert(states.REFF == nil and states.ArmorVariantManager and states.ShapeKeySystem, "release only REFF slot")
    blocked = true; callbacks.update()
    local replacement = { OtherPanel = true }
    rawset(_G, "__AVM_UI_INPUT_BLOCKER_STATES", replacement); callbacks.update()
    assert(states.REFF == nil and replacement.REFF == true and replacement.OtherPanel, "shared table replacement relinquishes old lease")
    replacement.REFF = "foreign value"; callbacks.update(); callbacks.update()
    assert(replacement.REFF == "foreign value" and #warnings == 2, "invalid REFF slot not overwritten")
    blocked = false; callbacks.update()
    assert(replacement.REFF == "foreign value", "changed slot not removed")
    rawset(_G, "__AVM_UI_INPUT_BLOCKER_STATES", 42); blocked = true; callbacks.update(); callbacks.update()
    assert(rawget(_G, "__AVM_UI_INPUT_BLOCKER_STATES") == 42 and #warnings == 3, "invalid shared value not overwritten")
    replacement.REFF = nil; rawset(_G, "__AVM_UI_INPUT_BLOCKER_STATES", replacement); callbacks.update()
    callbacks.reset(); callbacks.reset(); callbacks.update()
    assert(replacement.REFF == nil and replacement.OtherPanel and not ready, "reset removes only own lease and prevents reactivation")
    -- 旧 DLL 没有 drain 查询时仅回退可见性，不伪装为完整按键隔离能力。
    test_native.is_input_blocking = nil
    test_native.is_visible = function() return blocked end
    dofile(root .. "/lua/REFF.lua"); callbacks.update()
    assert(replacement.REFF == true, "legacy visibility fallback registers lease")
    blocked = false; callbacks.update(); callbacks.reset()
    assert(replacement.REFF == nil and replacement.OtherPanel, "legacy fallback cleanup")
end)
for _, name in ipairs(global_names) do rawset(_G, name, saved_globals[name].value) end
assert(blocker_ok, blocker_error)
print("Lua bridge and input coordination checks passed")
