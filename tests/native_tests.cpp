#include "frame.hpp"
#include "viewport.hpp"
#include <atomic>
#include <thread>
#include "page_identity.hpp"
#include "../native/plugin/panel_resize.hpp"
#include "../native/plugin/host_health.hpp"
#include "../native/plugin/mouse_click.hpp"
#include "../native/plugin/input_ownership.hpp"
#include "../native/plugin/settings.hpp"
#include "../native/plugin/keyboard_capture.hpp"
#include "../native/plugin/keyboard_event.hpp"
#include "../native/plugin/game_profile.hpp"
#include "ime_session.hpp"
#include "window_regions.hpp"
#include <iostream>
#include <cstring>
#include <filesystem>
#include <fstream>

// 必要边界测试：断包重组、非法长度、队列背压，以及跨句柄画面快照一致性。
int main() {
    int checks = 0;
    auto require = [&](bool passed, const char* name) {
        ++checks; if (!passed) throw std::runtime_error(name);
    };
    try {
        // 使用实际生产快照类，验证发送失败、重开确认和并发读写都不会拼接尺寸/代次。
        reff::ViewportState viewport;
        auto snapshot = viewport.snapshot();
        require(snapshot.width == 960 && snapshot.height == 640 && snapshot.generation == 1, "initial viewport tuple");
        require(!viewport.update(1700, 1025, [](auto) { return false; }) && viewport.snapshot().generation == 1,
            "queue failure does not commit viewport");
        require(!viewport.update(960, 640, [](auto) { throw std::runtime_error("duplicate sent"); return true; }),
            "unchanged viewport does not send");
        std::atomic_bool writing{true}, coherent{true};
        std::jthread observer([&] {
            std::uint64_t previous = 1;
            do {
                const auto current = viewport.snapshot();
                if (current.generation < previous || (current.generation > 1 &&
                    (current.width != 600 + int((current.generation - 2) % 300) || current.height != current.width + 100)))
                    coherent = false;
                previous = current.generation;
            } while (writing.load());
        });
        for (int i = 0; i < 5000; ++i) viewport.update(600 + i % 300, 700 + i % 300, [](auto) { return true; });
        writing = false; observer.join();
        require(coherent && viewport.snapshot().generation == 5001, "concurrent handshake sees whole viewport tuple");
        snapshot = viewport.snapshot();
        require(viewport.refresh([](auto) { return true; }) && viewport.snapshot().width == snapshot.width &&
            viewport.snapshot().height == snapshot.height && viewport.snapshot().generation == snapshot.generation + 1,
            "hotkey refresh confirms unchanged size with new generation");
        // 默认正式构建接受全部目录目标及通用目标，未测试状态不影响启用；保留实际后端检查。
        for (const auto& entry : reff::game_catalog) {
            const auto profile = reff::game_profile_for(entry.target);
            require(profile.known && reff::compatibility_decision(profile, true) == reff::CompatibilityDecision::allowed,
                "default build accepts every catalog target");
            require(reff::compatibility_decision(profile, false) == reff::CompatibilityDecision::renderer_mismatch,
                "all targets still require actual D3D12");
        }
        const auto wilds = reff::game_profile_for("mhwilds");
        const auto re4 = reff::game_profile_for("RE4");
        require(wilds.status == reff::SupportStatus::tested && reff::should_install_direct_input(wilds), "Wilds tested input policy");
        require(re4.status == reff::SupportStatus::untested && !reff::should_install_direct_input(re4), "RE4 untested input policy");
        const auto generic = reff::game_profile_for("FUTURE_RE_GAME");
        require(!generic.known && !reff::should_install_direct_input(generic) && generic.ime == reff::ImePolicy::proxy &&
            reff::compatibility_decision(generic, true) == reff::CompatibilityDecision::allowed, "generic REFramework target");
        require(reff::compatibility_decision(reff::game_profile_for(""), true) == reff::CompatibilityDecision::unknown_target,
            "missing REFramework target rejected");
        require(reff::compatibility_decision(reff::game_profile_for("../invalid"), true) == reff::CompatibilityDecision::unknown_target,
            "invalid target rejected");
        require(reff::starts_new_ime_session({}, "embedded:example.vue:text", true), "new IME session primes浏览器focus");
        require(!reff::starts_new_ime_session("embedded:example.vue:text", "embedded:example.vue:text", true), "repeat IME report preserves proxy focus");
        require(!reff::starts_new_ime_session("embedded:example.vue:text", "embedded:example.vue:text", false), "IME deactivation does not prime focus");
        // 用可控时钟覆盖无握手、正常连接、断管和退出，避免依赖 15 秒真实等待。
        reff::HostHealth health;
        health.started(100);
        require(!health.failure(15099, false, false, false), "startup grace period");
        require(health.failure(15100, false, false, true), "pipe alone is not a handshake");
        require(!health.failure(20000, false, true, true), "healthy host has no lifetime timeout");
        require(health.failure(20001, false, true, false), "live process with broken pipe fails");
        require(health.failure(101, true, false, false), "early process exit fails");
        health.started(30000);
        require(!health.failure(30001, false, false, false), "manual retry gets fresh deadline");
        // 使用真实命名管道验证断线标志与新会话连接，旧队列不能跨启动重放。
        {
            reff::Channel server, client;
            std::atomic_int received_count{};
            auto wait_for = [](auto predicate) {
                const auto deadline = GetTickCount64() + 3000;
                while (!predicate() && GetTickCount64() < deadline) Sleep(5);
                return predicate();
            };
            auto connect = [&] {
                const auto id = reff::make_session();
                server.start(id, true, [](reff::Json) {});
                client.start(id, false, [&](reff::Json) { ++received_count; });
                require(wait_for([&] { return server.connected() && client.connected(); }), "real pipe connection");
            };
            connect();
            client.stop();
            require(wait_for([&] { return !server.connected(); }), "peer exit clears connection flag");
            server.send({{"type", "stale"}});
            connect();
            server.send({{"type", "fresh"}});
            require(wait_for([&] { return received_count.load() >= 1; }), "fresh session receives message");
            client.stop(); server.stop();
            require(received_count.load() == 1, "disconnected queue was not replayed");
        }
        // 覆盖身份伪造和同 frame 重载；独立测试不替代真实浏览器文档生命周期验收。
        const reff::Json page_request{{"method", "example.counter.get"}, {"params", reff::Json::object()}};
        require(reff::valid_page_request(page_request), "minimal page request accepted");
        for (const auto* field : {"pluginId", "pageId", "frameId", "sessionId", "epoch", "id", "type"}) {
            auto forged = page_request; forged[field] = "forged";
            require(!reff::valid_page_request(forged), "forged identity rejected");
        }
        auto malformed = page_request; malformed["params"] = nullptr;
        require(!reff::valid_page_request(malformed), "null page params rejected");
        reff::PageIdentity owner{7, "frame-a", 2, "shell:2"};
        require(owner.matches(7, "frame-a", 2), "live page accepted");
        require(!owner.matches(8, "frame-a", 2), "other browser rejected");
        require(!owner.matches(7, "frame-b", 2), "other frame rejected");
        require(!owner.matches(7, "frame-a", 3), "reloaded document rejected");
        require(!reff::PageIdentity{}.matches(0, "", 0), "unbound page rejected");
        reff::Json request{{"id", "1"}, {"method", "example.counter.get"}, {"params", reff::Json::object()}};
        auto encoded = reff::encode(request); reff::Decoder decoder;
        std::vector<reff::Json> received;
        for (char byte : encoded) for (auto& value : decoder.feed(&byte, 1)) received.push_back(value);
        require(received.size() == 1 && received[0] == request, "fragmented frame");
        auto joined = encoded + encoded; require(decoder.feed(joined.data(), joined.size()).size() == 2, "coalesced frames");
        bool rejected = false;
        try { reff::Decoder bad; const char bytes[]{-1,-1,-1,-1}; bad.feed(bytes, 4); } catch (...) { rejected = true; }
        require(rejected, "oversized prefix");
        require(reff::valid_request(request), "valid request");
        request["method"] = "bad method"; require(!reff::valid_request(request), "method syntax");
        reff::Queue<int> queue;
        for (int i = 0; i < reff::queue_limit; ++i) require(queue.push(i), "queue capacity");
        require(!queue.push(999), "queue backpressure");
        require(queue.pop().value() == 0, "queue FIFO"); queue.clear(); require(!queue.pop(), "queue clear");
        // 纯窗口消息边界：模拟显隐/失焦/穿透改变；不读硬件状态、不启动游戏或注入输入。
        {
            using Ownership = reff::InputOwnership;
            Ownership inputs;
            auto legacy = [&](UINT message, WPARAM key, LPARAM native, bool game) {
                return inputs.route(Ownership::legacy(message, key, native).view(), game);
            };
            auto mouse = [&](UINT message, bool game, WPARAM buttons = 0) {
                return legacy(message, buttons, 0, game);
            };
            require(mouse(WM_LBUTTONDOWN, true), "suspended activation click down belongs to game");
            require(mouse(WM_LBUTTONUP, false), "activation click up passes after focus restoration");
            require(!inputs.has_reff_owned(), "game activation click never extends blocker");
            require(!mouse(WM_RBUTTONDOWN, false), "visible mouse down belongs to REFF");
            require(inputs.has_reff_owned(), "REFF mouse down extends blocker");
            require(!mouse(WM_RBUTTONDOWN, true), "closed mouse repeated down retains REFF owner");
            require(!mouse(WM_RBUTTONUP, true), "closed mouse up retains REFF owner");
            require(!inputs.has_reff_owned(), "last release clears pending blocker");
            require(mouse(WM_RBUTTONUP, false), "unknown mouse up passes even while visible");
            require(!mouse(WM_LBUTTONDBLCLK, false) && !mouse(WM_LBUTTONUP, true), "double click is a paired down");
            require(!mouse(WM_MBUTTONDBLCLK, false) && !mouse(WM_MBUTTONUP, true), "middle double click ownership without browser support");
            const auto x1 = MAKEWPARAM(0, XBUTTON1), x2 = MAKEWPARAM(0, XBUTTON2);
            require(!mouse(WM_XBUTTONDBLCLK, false, x1), "X1 double click down captured");
            require(mouse(WM_XBUTTONUP, false, x2), "unknown X2 up does not release X1");
            require(inputs.has_reff_owned() && !mouse(WM_XBUTTONUP, true, x1), "X1 up retains own identity");
            constexpr LPARAM a_native = 0x1eLL << 16;
            require(!legacy(WM_KEYDOWN, 'A', a_native, false), "visible keyboard down captured");
            require(inputs.character_route(WM_CHAR, a_native) == std::optional<bool>{false}, "captured character follows held scan not character value");
            require(inputs.character_route(WM_SYSCHAR, a_native | (1LL << 30)) == std::optional<bool>{false}, "closed held system repeat character swallowed");
            require(!inputs.character_route(WM_CHAR, a_native | (1LL << 24)).has_value(), "character extended bit must match owner");
            require(!inputs.character_route(WM_CHAR, 0).has_value(), "zero-scan IME character has no physical owner");
            require(!inputs.character_route(WM_IME_CHAR, a_native).has_value(), "IME character is not treated as physical key repeat");
            require(!legacy(WM_KEYDOWN, 'A', a_native | (1LL << 30), true), "closed keyboard repeat swallowed");
            require(!legacy(WM_KEYUP, 'A', a_native | (3LL << 30), true), "closed keyboard up swallowed");
            require(!inputs.character_route(WM_CHAR, a_native).has_value(), "late character after up has no reliable owner");
            require(legacy(WM_KEYDOWN, 'A', a_native, true) && inputs.character_route(WM_CHAR, a_native) == std::optional<bool>{true}, "game-owned character passes when panel opens");
            require(legacy(WM_KEYUP, 'A', a_native, false), "game-owned character sequence release passes");
            require(legacy(WM_KEYUP, 'A', a_native | (3LL << 30), false), "unknown keyboard up passes");
            constexpr LPARAM ctrl_native = 0x1dLL << 16;
            require(!legacy(WM_SYSKEYDOWN, VK_CONTROL, ctrl_native, false), "legacy system down captured");
            require(legacy(WM_SYSKEYUP, VK_CONTROL, ctrl_native | (1LL << 24), false), "extended key is distinct");
            require(!legacy(WM_KEYUP, VK_CONTROL, ctrl_native, true), "system and normal messages share key identity");

            RAWKEYBOARD keyboard{}; keyboard.VKey = 'A'; keyboard.MakeCode = 0x1e;
            auto raw_key = [&](bool game, std::uintptr_t device = 1) {
                return inputs.route(Ownership::raw_keyboard(keyboard, device).view(), game);
            };
            require(!raw_key(false), "raw keyboard down captured");
            require(legacy(WM_KEYUP, 'A', a_native, false) && inputs.has_reff_owned(), "legacy up cannot clear raw held key");
            keyboard.Message = WM_KEYUP; // direction is Flags, not the advisory Message field.
            require(!raw_key(true), "raw repeat direction ignores contradictory message");
            keyboard.Flags = RI_KEY_BREAK | RI_KEY_E0;
            require(raw_key(false), "raw E0 is distinct from unextended key");
            keyboard.Flags = RI_KEY_BREAK | RI_KEY_E1;
            require(raw_key(false), "raw E1 is distinct from E0 and unextended key");
            keyboard.Flags = RI_KEY_BREAK;
            require(raw_key(false, 2) && inputs.has_reff_owned(), "raw devices keep independent ownership");
            ++keyboard.MakeCode;
            require(raw_key(false), "raw scan code is an identity component");
            --keyboard.MakeCode;
            keyboard.VKey = 'B';
            require(raw_key(false), "raw VKey is an identity component");
            keyboard.VKey = 'A';
            require(!raw_key(true) && !inputs.has_reff_owned(), "raw break releases original owner");
            keyboard.Flags = RI_KEY_E0;
            require(raw_key(true), "raw key game down");
            keyboard.Flags |= RI_KEY_BREAK;
            require(raw_key(false), "raw game break passes after panel opens");
            keyboard.VKey = 255;
            require(Ownership::raw_keyboard(keyboard).size == 0, "invalid raw VKey has no held owner");
            keyboard.VKey = 'A'; keyboard.MakeCode = Ownership::overrun_make_code;
            require(Ownership::raw_keyboard(keyboard).size == 0, "raw keyboard overrun ignored");
            require(Ownership::legacy(WM_CHAR, 'A', 0).size == 0, "character messages are not held keys");

            RAWMOUSE raw{};
            auto raw_mouse = [&](USHORT flags, bool game, std::uintptr_t device = 1) {
                raw.usButtonFlags = flags;
                return inputs.route(Ownership::raw_mouse(raw, device).view(), game);
            };
            require(raw_mouse(RI_MOUSE_LEFT_BUTTON_DOWN, true), "raw activation down passes");
            require(!mouse(WM_LBUTTONDOWN, false), "legacy down remains independent of raw owner");
            require(raw_mouse(RI_MOUSE_LEFT_BUTTON_UP, false), "raw activation up passes after restore");
            require(!mouse(WM_LBUTTONUP, true), "legacy captured up stays swallowed");
            require(!raw_mouse(RI_MOUSE_RIGHT_BUTTON_DOWN, false), "raw REFF right down");
            require(raw_mouse(RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_BUTTON_4_DOWN, false), "mixed unknown release and new down passes entire raw packet");
            require(raw_mouse(RI_MOUSE_BUTTON_4_UP, false), "mixed new down committed to actual game route");
            require(!raw_mouse(RI_MOUSE_RIGHT_BUTTON_UP, true), "independent raw REFF release swallowed");
            require(!raw_mouse(RI_MOUSE_RIGHT_BUTTON_DOWN, false), "mixed conflict REFF owner setup");
            require(raw_mouse(RI_MOUSE_LEFT_BUTTON_DOWN, true), "mixed conflict game owner setup");
            require(raw_mouse(RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_RIGHT_BUTTON_UP, false), "mixed existing game release wins over REFF release");
            require(!inputs.has_reff_owned(), "mixed releases retire both held owners");
            require(!raw_mouse(RI_MOUSE_RIGHT_BUTTON_DOWN, false), "REFF-only packet setup");
            require(!raw_mouse(RI_MOUSE_RIGHT_BUTTON_UP | RI_MOUSE_BUTTON_4_DOWN, true), "REFF release blocks whole packet despite new down passthrough default");
            require(!raw_mouse(RI_MOUSE_BUTTON_4_UP, true), "blocked mixed new down commits actual REFF owner");
            require(raw_mouse(RI_MOUSE_LEFT_BUTTON_DOWN, true), "mixed repeat game owner setup");
            require(raw_mouse(RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_BUTTON_4_DOWN, false), "game-owned repeated down forces mixed packet passthrough");
            require(raw_mouse(RI_MOUSE_BUTTON_4_UP | RI_MOUSE_LEFT_BUTTON_UP, false), "game-forced new down owns its later release");
            require(!raw_mouse(RI_MOUSE_BUTTON_5_DOWN, false), "raw X2 down captured");
            require(!raw_mouse(RI_MOUSE_BUTTON_5_DOWN, true), "raw repeated down preserves owner after closing");
            require(raw_mouse(RI_MOUSE_BUTTON_5_UP, false, 2) && inputs.has_reff_owned(), "raw mouse device is distinct");
            require(!raw_mouse(RI_MOUSE_BUTTON_5_UP, true), "raw X2 up swallowed after closing");
            raw.usButtonFlags = RI_MOUSE_WHEEL; raw.usFlags = MOUSE_MOVE_ABSOLUTE; raw.lLastX = 100;
            require(Ownership::raw_mouse(raw).size == 0, "wheel and movement have no held ownership");
            require(raw_mouse(RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP, false), "same packet down-up ambiguity prioritizes unknown release");
            require(!inputs.has_reff_owned(), "no stuck REFF owner after paired packet");
        }
        // Windows 将快速第二次按下替换为 DBLCLK；浏览器 的按下与抬起必须保持相同点击次数。
        reff::MouseClickTracker clicks;
        auto left_down = clicks.translate(WM_LBUTTONDOWN);
        auto left_up = clicks.translate(WM_LBUTTONUP);
        auto double_down = clicks.translate(WM_LBUTTONDBLCLK);
        auto double_up = clicks.translate(WM_LBUTTONUP);
        require(left_down && !left_down->mouse_up && left_down->button == 0 && left_down->click_count == 1,
            "left down translated");
        require(left_up && left_up->mouse_up && left_up->click_count == 1, "left up paired");
        require(double_down && !double_down->mouse_up && double_down->click_count == 2, "double click down translated");
        require(double_up && double_up->mouse_up && double_up->click_count == 2, "double click up paired");
        auto right_down = clicks.translate(WM_RBUTTONDBLCLK);
        auto right_up = clicks.translate(WM_RBUTTONUP);
        require(right_down && right_down->button == 1 && right_down->click_count == 2, "right double click translated");
        require(right_up && right_up->mouse_up && right_up->click_count == 2, "right double click up paired");
        require(!clicks.translate(WM_MOUSEMOVE), "non-click ignored");
        require(reff::should_block_keyboard(true, false, false), "visible keyboard capture");
        require(!reff::should_block_keyboard(true, true, false), "keyboard passthrough");
        require(reff::should_block_keyboard(true, true, true), "text focus overrides keyboard passthrough");
        require(!reff::should_block_keyboard(false, false, true), "hidden panel releases keyboard");
        require(reff::input_modifiers(false, true, false) == 4, "control modifier uses the浏览器control bit");
        require(reff::input_modifiers(false, false, false, false, false, true) == 128, "meta modifier uses the浏览器command bit");
        require(reff::input_modifiers(true, true, true, true, true) == 94, "combined input modifiers preserve every浏览器flag");
        // Core 设置必须拒绝未知字段、原子落盘，并按客户区比例恢复窗口几何。
        {
            const auto directory = std::filesystem::temp_directory_path() / (L"reff-settings-test-" + std::to_wstring(GetCurrentProcessId()));
            const auto path = directory / L"settings.json";
            std::error_code ignored; std::filesystem::remove_all(directory, ignored);
            reff::SettingsStore settings; std::string warning, error;
            settings.open(path, warning);
            require(warning.empty() && settings.snapshot().value("language", "") == "zh-CN" &&
                settings.snapshot()["appearance"].value("textScale", 0.0) == 1.0 &&
                settings.snapshot()["startup"].value("preload", false), "settings defaults");
            require(settings.snapshot()["hotkey"]["key"]==VK_HOME && settings.snapshot()["hotkey"]["modifiers"]==0, "new settings default to Home");
            require(settings.update({{"hotkey",{{"key",VK_F8},{"modifiers",0}}}},error), "existing F8 binding accepted");
            require(!settings.update({{"unknown", true}}, error), "unknown settings rejected");
            require(settings.plugin_layout("example.vue")["padding"]==24, "default plugin padding is 24");
            require(settings.set_plugin_padding("example.vue",0,error) && settings.plugin_layout("example.vue")["padding"]==0,
                "plugin zero override");
            require(settings.set_plugin_padding("example.react",48,error) && settings.plugin_layout("example.vue")["padding"]==0,
                "plugin overrides do not clobber other plugins");
            require(!settings.set_plugin_padding("example.vue",49,error) && !settings.set_plugin_padding("example.vue",1.5,error), "plugin range validation");
            require(settings.set_plugin_padding("example.vue",nullptr,error) && settings.plugin_layout("example.vue")["override"].is_null(), "plugin inherits after null");
            require(settings.plugin_layout("example.react")["padding"]==48, "clearing preserves another plugin override");
            require(settings.snapshot()["layout"]["sidebarCollapsed"]==false && settings.snapshot()["layout"]["headerCollapsed"]==true &&
                settings.snapshot()["layout"]["sidebarCompact"]==true && settings.snapshot()["layout"]["headerCompact"]==false &&
                settings.snapshot()["navigation"]["rememberLastPlugin"]==true, "independent compact and auto-hide defaults");
            require(settings.update({{"layout",{{"sidebarCollapsed",true},{"sidebarCompact",false},{"headerCompact",true},{"pluginPadding",0}}},{"navigation",{{"lastPluginId","example.vue"}}}},error), "layout navigation patch");
            require(!settings.update({{"layout",{{"sidebarCompact",1}}}},error), "compact requires boolean");
            require(!settings.update({{"layout",{{"headerCompact",nullptr}}}},error), "header compact requires boolean");
            require(!settings.update({{"layout",{{"pluginPadding",49}}}},error), "padding upper bound");
            require(!settings.update({{"layout",{{"pluginPadding",1.5}}}},error), "padding integer required");
            require(!settings.update({{"navigation",{{"lastPluginId","../bad"}}}},error), "navigation id rejected");
            error.clear();
            require(settings.update({{"language", "en-US"}, {"appearance", {{"surfaceBlur", 32}, {"textScale", 1.4}}},
                {"startup", {{"preload", false}}}, {"input", {{"mousePassthrough", true}}}}, error), "settings patch accepted");
            error.clear();
            require(!settings.update({{"appearance", {{"surfaceBlur", 33}}}}, error), "settings blur upper bound");
            settings.set_geometry({1920, 1080, 480, 220, 960, 640});
            require(settings.save(error), "settings atomically saved");
            reff::SettingsStore loaded; loaded.open(path, warning);
            const auto restored = loaded.restored_geometry(2560, 1440);
            require(warning.empty() && loaded.snapshot()["input"].value("mousePassthrough", false) &&
                !loaded.snapshot()["startup"].value("preload", true), "settings persisted");
            require(loaded.snapshot()["hotkey"]["key"]==VK_F8, "existing saved F8 binding retained after upgrade");
            require(restored && restored->left == 640 && restored->top == 293 && restored->width == 1280 && restored->height == 853,
                "settings geometry scaled");
            require(loaded.snapshot()["layout"]["pluginPadding"]==0 && loaded.snapshot()["navigation"]["lastPluginId"]=="example.vue", "layout and last plugin persisted");
            require(loaded.plugin_layout("example.react")["padding"]==48, "plugin override persisted");
            require(loaded.snapshot()["layout"]["sidebarCollapsed"]==true && loaded.snapshot()["layout"]["sidebarCompact"]==false && loaded.snapshot()["layout"]["headerCompact"]==true, "independent chrome preferences persisted");
            auto previous=loaded.snapshot();previous["layout"]["pluginPadding"]=12;previous["layout"].erase("pluginPaddingOverrides");previous["layout"].erase("sidebarCompact");previous["layout"].erase("headerCompact");
            { std::ofstream stream(path);stream<<previous.dump(); }
            loaded.open(path,warning);
            require(loaded.plugin_layout("example.vue")["padding"]==12 && loaded.snapshot()["layout"]["pluginPaddingOverrides"].empty(), "previous explicit padding preserved");
            require(loaded.snapshot()["layout"]["sidebarCollapsed"]==true && loaded.snapshot()["layout"]["sidebarCompact"]==true && loaded.snapshot()["layout"]["headerCompact"]==false, "legacy auto-hide preferences retained with compact defaults");
            auto old=loaded.snapshot();old.erase("layout");old.erase("navigation");
            { std::ofstream stream(path);stream<<old.dump(); }
            loaded.open(path,warning);
            require(warning.empty() && loaded.snapshot()["language"]=="en-US" && loaded.snapshot()["layout"]["sidebarCollapsed"]==false &&
                loaded.snapshot()["navigation"]["rememberLastPlugin"]==true, "old settings migrate without losing user values");
            std::filesystem::remove_all(directory, ignored);
        }
        // 紧凑标题只在 Shell 声明的空白区拖动，按钮、插件内容与越界区域必须留给网页。
        {
            reff::Json value={{"viewportWidth",960},{"viewportHeight",640},{"rects",reff::Json::array({{{"x",100},{"y",0},{"width",800},{"height",40}}})}};
            const auto regions=reff::WindowRegions::parse(value);
            require(regions && regions->contains(200,20,960,640), "reported title hit");
            require(regions->contains(400,40,1920,1280), "scaled title hit");
            require(!regions->contains(80,20,960,640) && !regions->contains(920,20,960,640) && !regions->contains(200,45,960,640), "chrome buttons and content excluded");
            value["rects"][0]["width"]=961; require(!reff::WindowRegions::parse(value), "out of bounds region rejected");
        }
        auto session = reff::make_session(); reff::SharedFrame writer, reader;
        writer.open(session, true); reader.open(session, false);
        std::vector<std::uint8_t> source(reff::panel_width * reff::panel_height * 4, 0x7f), result;
        std::uint64_t sequence = 0;
        require(!reader.read(result, sequence), "unpainted frame");
        require(!writer.write(source.data(), 1, 1), "invalid dimensions");
        require(writer.write(source.data(), reff::panel_width, reff::panel_height), "write frame");
        require(reader.read(result, sequence) && result == source && sequence == 1, "frame snapshot");
        require(writer.write(source.data(), reff::panel_width, reff::panel_height), "duplicate frame write");
        require(!reader.read(result, sequence) && sequence == 1, "duplicate frame skipped");
        // 首帧全透明仍需发布，随后相同内容才去重；覆盖零初始化共享区被误认作有效帧的回归。
        {
            reff::SharedFrame transparent_writer, transparent_reader;
            const auto transparent_session = reff::make_session();
            transparent_writer.open(transparent_session, true); transparent_reader.open(transparent_session, false);
            std::vector<std::uint8_t> zero_frame(reff::panel_width * reff::panel_height * 4), snapshot;
            std::uint64_t zero_sequence = 0;
            require(!transparent_reader.read(snapshot, zero_sequence), "transparent frame not published yet");
            require(transparent_writer.write(zero_frame.data(), reff::panel_width, reff::panel_height), "publish transparent first frame");
            require(transparent_reader.read(snapshot, zero_sequence) && zero_sequence == 1 && snapshot == zero_frame, "transparent first frame available");
            require(transparent_writer.write(zero_frame.data(), reff::panel_width, reff::panel_height), "repeat transparent frame");
            require(!transparent_reader.read(snapshot, zero_sequence) && zero_sequence == 1, "transparent duplicate deduplicated");
        }
        std::vector<std::uint8_t> resized(640 * 480 * 4, 0x42); int frame_width{}, frame_height{};
        require(writer.write(resized.data(), 640, 480), "write resized frame");
        require(reader.read(result, sequence, frame_width, frame_height) && result == resized && frame_width == 640 && frame_height == 480,
            "resized frame snapshot");
        require(!reader.read(result, sequence), "unchanged frame skipped");
        // viewport 代次变化必须生成新快照，即使尺寸和像素完全相同，渲染器才能确认浏览器已响应本次重排。
        std::uint64_t viewport_generation{};
        require(writer.write(resized.data(), 640, 480, 2), "write new viewport generation");
        require(reader.read(result, sequence, frame_width, frame_height, viewport_generation) && viewport_generation == 2,
            "viewport generation snapshot");
        require(writer.write(resized.data(), 640, 480, 3), "write unmatched viewport generation");
        auto before = sequence;
        require(!reader.read(result, sequence, frame_width, frame_height, viewport_generation, 2) && sequence == before,
            "unmatched viewport generation is retained");
        reff::PanelResizeController resize;
        RECT start{100, 100, 1060, 740}, next{};
        require(resize.begin(start, 2, 1060, 400, reff::PanelResizeController::InputMode::absolute), "resize begin");
        require(resize.update(1160, 400, reff::PanelResizeController::InputMode::absolute, next) && next.right == 1160,
            "absolute resize update");
        require(!resize.update(1200, 400, reff::PanelResizeController::InputMode::relative, next), "foreign input rejected");
        require(resize.end() && !resize.end(), "resize end idempotent");
        std::cout << checks << " checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
