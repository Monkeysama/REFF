-- REFF 的 Lua 调度层：仅在调用者指定的游戏更新阶段运行，不从浏览器线程访问游戏。
local Bridge = {}

-- 创建独立桥接实例，依赖注入方便在真实 Lua 5.4 中验证重置、异常和参数行为。
function Bridge.create(native, codec, clock, handlers)
    assert(type(native) == "table", "REFF native bridge missing")
    assert(type(codec) == "table", "REFF JSON codec missing")
    local active = true
    local instance = {}

    -- 一次处理最多 32 条；时间预算只决定是否继续，不会抢占正在运行的游戏函数。
    function instance.update()
        if not active then return end
        local started = clock()
        for _ = 1, 32 do
            local raw = native.poll()
            if raw == nil then break end
            local parsed, request = pcall(codec.load, raw)
            if parsed and type(request) == "table" then
                local response = { type = "response", id = request.id, epoch = request.epoch }
                local handler, plugin_id = handlers[request.method]
                if handler == nil and type(handlers.resolve) == "function" then
                    handler, plugin_id = handlers.resolve(request.method, request)
                end
                if plugin_id then response.pluginId = plugin_id end
                if handler == nil then
                    response.error = { code = "METHOD_NOT_FOUND", message = "方法尚未注册" }
                else
                    local ok, result = pcall(handler, request.params)
                    if ok then
                        response.result = result
                    else
                        response.error = { code = "HANDLER_ERROR", message = tostring(result) }
                    end
                end
                local encoded, message = pcall(codec.dump, response)
                if encoded then native.respond(message) end
            end
            if clock() - started >= 0.00025 then break end
        end
    end

    -- 脚本重置后停止取请求；原生端负责 epoch 失效与旧队列清理。
    function instance.close()
        if not active then return end
        active = false
        native.set_ready(false)
    end

    native.set_ready(true)
    return instance
end

return Bridge
