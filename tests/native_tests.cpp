#include "frame.hpp"
#include "page_identity.hpp"
#include "../native/plugin/panel_resize.hpp"
#include "../native/plugin/host_health.hpp"
#include "../native/plugin/mouse_click.hpp"
#include "../native/plugin/settings.hpp"
#include "../native/plugin/keyboard_capture.hpp"
#include <iostream>
#include <cstring>
#include <filesystem>

// 必要边界测试：断包重组、非法长度、队列背压，以及跨句柄画面快照一致性。
int main() {
    int checks = 0;
    auto require = [&](bool passed, const char* name) {
        ++checks; if (!passed) throw std::runtime_error(name);
    };
    try {
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
        // 覆盖身份伪造和同 frame 重载；独立测试不替代真实 CEF 文档生命周期验收。
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
        // Windows 将快速第二次按下替换为 DBLCLK；CEF 的按下与抬起必须保持相同点击次数。
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
            require(!settings.update({{"unknown", true}}, error), "unknown settings rejected");
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
            require(restored && restored->left == 640 && restored->top == 293 && restored->width == 1280 && restored->height == 853,
                "settings geometry scaled");
            std::filesystem::remove_all(directory, ignored);
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
        // viewport 代次变化必须生成新快照，即使尺寸和像素完全相同，渲染器才能确认 CEF 已响应本次重排。
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
