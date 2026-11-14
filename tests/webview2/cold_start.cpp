#include "frame.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace reff;
namespace {
// 本测试只拥有独立宿主、Job、缓存和 IPC；主线程验证帧，IPC 工作线程仅投递到有界队列。
// 不启用 selftest、不注入网页脚本，也不访问或操作游戏、系统浏览器和用户输入。
Json scenario(const std::filesystem::path& host, const std::filesystem::path& output, int run) {
    Json report = {{"run",run},{"passed",false},{"gameValidated",false},{"selfTestEnabled",false},
        {"scriptInjected",false},{"messages",Json::array()},{"phases",Json::array()}};
    const auto begun = GetTickCount64();
    try {
        if (std::filesystem::exists(output)) throw std::runtime_error("run output already exists");
        const auto cache = output/L"data/REFF/cache", manifests = output/L"plugins";
        std::filesystem::create_directories(cache);
        std::filesystem::create_directories(manifests);
        const auto events = output/L"data/REFF/log/webview2/events.jsonl";
        const auto session = make_session();
        report["sessionId"] = narrow(session);
        SharedFrame frame; frame.open(session,true);
        Queue<Json> incoming;
        Channel channel;
        channel.start(session,true,[&](Json value){ incoming.push(std::move(value)); });
        // Job 的生命周期覆盖异常路径；关闭时只回收本轮创建的宿主及其子进程。
        Handle job(CreateJobObjectW(nullptr,nullptr));
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job || !SetInformationJobObject(job.get(),JobObjectExtendedLimitInformation,&limits,sizeof(limits)))
            throw std::runtime_error("job creation failed");
        auto command = L"\""+host.wstring()+L"\" --reff-session="+session+
            L" --reff-cache=\""+cache.wstring()+L"\" --reff-manifests=\""+manifests.wstring()+L"\" --reff-game=MHWILDS";
        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(host.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,
            nullptr,nullptr,&startup,&pi)) throw std::runtime_error("host launch failed");
        Handle process(pi.hProcess), thread(pi.hThread);
        if (!AssignProcessToJobObject(job.get(),process.get())) {
            TerminateProcess(process.get(),1); // 只处理尚未入 Job 的本轮挂起进程。
            throw std::runtime_error("job assignment failed");
        }
        if (ResumeThread(thread.get()) == DWORD(-1)) throw std::runtime_error("host resume failed");
        thread.reset();
        auto send = [&](Json value) {
            report["messages"].push_back({{"elapsedMs",GetTickCount64()-begun},{"message",value}});
            if (!channel.send(std::move(value))) throw std::runtime_error("control queue full");
        };
        bool hello = false;
        // Shell 请求失败由正常协议返回；本测试只验真实捕获，不伪造网页 ready/绘制确认。
        auto drain = [&] {
            while (auto value = incoming.pop()) {
                const auto type = value->value("type",std::string{});
                if (type == "hello") {
                    if (hello || value->value("protocol",0u) != protocol_version ||
                        value->value("sessionId",std::string{}) != narrow(session))
                        throw std::runtime_error("invalid or repeated handshake");
                    hello = true;
                    send({{"type","visible"},{"value",false}});
                    send({{"type","ready"},{"ready",true},{"epoch",1}});
                    send({{"type","viewport"},{"width",960},{"height",640},{"generation",2}});
                } else if (type == "request") {
                    send({{"type","response"},{"id",value->at("id")},{"epoch",1},
                        {"error",{{"code","NOT_SUPPORTED"},{"message","capture-only independent test"}}}});
                }
            }
            if (WaitForSingleObject(process.get(),0) == WAIT_OBJECT_0)
                throw std::runtime_error("host exited before completion");
        };
        // 正式宿主已有 navigation 日志是只读初始化屏障；不依赖 selftest diagnostic 或网页测试钩子。
        bool navigated = false;
        while (GetTickCount64()-begun < 15000 && !navigated) {
            drain();
            std::ifstream log(events); std::string line;
            while (std::getline(log,line)) {
                const auto entry = Json::parse(line,nullptr,false);
                if (entry.is_object() && entry.value("event",std::string{}) == "navigation" && entry.value("success",false))
                    navigated = true;
            }
            if (!navigated) Sleep(10);
        }
        if (!hello || !navigated) throw std::runtime_error("hidden initialization timed out");
        report["hiddenNavigationMs"] = GetTickCount64()-begun;
        std::uint64_t sequence{}, generation{};
        std::vector<std::uint8_t> pixels;
        int width{}, height{};
        // 隐藏超过生产捕获超时，且只允许旧的默认帧；A/B 不应在隐藏期间被调度发布。
        auto hold_hidden = [&](std::uint64_t first_deferred_generation, ULONGLONG duration_ms = 5600) {
            const auto hidden = GetTickCount64();
            while (GetTickCount64()-hidden < duration_ms) {
                drain();
                if (frame.read(pixels,sequence,width,height,generation) && generation >= first_deferred_generation)
                    throw std::runtime_error("deferred viewport published while hidden");
                Sleep(8);
            }
        };
        // 一次 visible=true 后，第一张新共享帧必须就是最新代次；绝不补发 visible/viewport 救活。
        // 要求真实 BGRA 中的不透明代次标记及完整尺寸，并继续观察 1 秒避免首次显示后立刻退出。
        auto show_once = [&](const char* name,int expected_width,int expected_height,std::uint64_t expected_generation) {
            frame.read(pixels,sequence,width,height,generation); // 丢弃隐藏期残留，不把旧共享帧当成恢复。
            const auto shown = GetTickCount64();
            send({{"type","visible"},{"value",true}});
            bool received = false; ULONGLONG first_ms{};
            while (GetTickCount64()-shown < 5000) {
                drain();
                if (frame.read(pixels,sequence,width,height,generation)) {
                    if (generation != expected_generation || width != expected_width || height != expected_height)
                        throw std::runtime_error("first/new visible frame is not the latest viewport");
                    const auto* stamp = pixels.data()+(22*width+98)*4;
                    const auto painted = std::uint32_t(stamp[2])|(std::uint32_t(stamp[1])<<8)|(std::uint32_t(stamp[0])<<16);
                    if (stamp[3] != 255 || painted != (expected_generation&0xffffffu))
                        throw std::runtime_error("shared frame lacks painted viewport acknowledgement");
                    if (!received) { received = true; first_ms = GetTickCount64()-shown; }
                }
                if (received && GetTickCount64()-shown >= first_ms+1000) break;
                Sleep(8);
            }
            if (!received) throw std::runtime_error("single-show first frame timed out (5s)");
            report["phases"].push_back({{"phase",name},{"width",width},{"height",height},{"generation",generation},
                {"firstFrameMs",first_ms},{"visibleTrueCount",1},{"rescueViewportCount",0},{"passed",true}});
        };
        // 导航后的观察窗口让旧版 STA 确实有机会调度 A，再交付 B；不能在同一批 IPC 中合并掉反例。
        hold_hidden(2,250);
        send({{"type","viewport"},{"width",1458},{"height",818},{"generation",3}});
        hold_hidden(2);
        show_once("cold-start",1458,818,3);
        send({{"type","visible"},{"value",false}});
        send({{"type","viewport"},{"width",1280},{"height",720},{"generation",4}});
        hold_hidden(4,250);
        send({{"type","viewport"},{"width",1700},{"height",1025},{"generation",5}});
        hold_hidden(4);
        show_once("hidden-resize-reopen",1700,1025,5);
        send({{"type","shutdown"}});
        if (WaitForSingleObject(process.get(),5000) != WAIT_OBJECT_0) throw std::runtime_error("graceful shutdown timed out");
        DWORD code{};
        if (!GetExitCodeProcess(process.get(),&code) || code != 0) throw std::runtime_error("host exit code is not zero");
        channel.stop();
        report["gracefulExit"] = true;
        report["passed"] = true;
    } catch (const std::exception& error) { report["error"] = error.what(); }
    report["elapsedMs"] = GetTickCount64()-begun;
    return report;
}
}

// 三轮各自拥有全新进程、会话和空缓存；保存失败样本，不以重试成功覆盖冷启动失败。
int wmain(int argc,wchar_t** argv) {
    if (argc != 3) return 2;
    try {
        const auto host = std::filesystem::absolute(argv[1]), output = std::filesystem::absolute(argv[2]);
        if (!std::filesystem::is_regular_file(host) || std::filesystem::exists(output))
            throw std::runtime_error("host missing or output already exists");
        std::filesystem::create_directories(output);
        Json report = {{"passed",true},{"gameValidated",false},{"runs",Json::array()}};
        for (int run = 1; run <= 3; ++run) {
            auto result = scenario(host,output/("run-"+std::to_string(run)),run);
            report["passed"] = report["passed"].get<bool>() && result["passed"].get<bool>();
            report["runs"].push_back(result);
            std::ofstream(output/("run-"+std::to_string(run)+".json")) << result.dump(2);
        }
        std::ofstream(output/"cold-start-summary.json") << report.dump(2);
        std::cout << report.dump(2) << '\n';
        return report["passed"].get<bool>() ? 0 : 1;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
