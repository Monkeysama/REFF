#include "frame.hpp"
#include "host_health.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace reff;
namespace {
std::filesystem::path fixture_assets,fixture_manifests;
// 每次故障都用全新 IPC 与缓存；Job 仅包含本驱动创建的宿主，绝不查杀系统浏览器或游戏。
Json scenario(const std::filesystem::path& host,const std::filesystem::path& output,const std::string& action) {
    std::filesystem::create_directories(output);
    Handle job(CreateJobObjectW(nullptr,nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!job || !SetInformationJobObject(job.get(),JobObjectExtendedLimitInformation,&limits,sizeof(limits)))
        throw std::runtime_error("test Job creation failed");
    const auto session=make_session();SharedFrame frame;frame.open(session,true);
    Queue<Json> incoming;Channel channel;
    channel.start(session,true,[&](Json message){incoming.push(std::move(message));});
    const auto cache=output/L"data"/L"REFF"/L"cache";
    std::filesystem::create_directories(cache);
    auto command=L"\""+host.wstring()+L"\" --selftest --reff-session="+session+L" --reff-cache=\""+cache.wstring()+L"\"";
    command+=L" --reff-assets=\""+fixture_assets.wstring()+
        L"\" --reff-manifests=\""+fixture_manifests.wstring()+L"\" --reff-game=MHWILDS";
    STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(host.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))
        throw std::runtime_error("test host launch failed");
    Handle owner(process.hProcess),thread(process.hThread);
    if(!AssignProcessToJobObject(job.get(),owner.get())) {TerminateProcess(owner.get(),1);throw std::runtime_error("test Job assignment failed");}
    ResumeThread(thread.get());thread.reset();
    const auto began=GetTickCount64();bool hello=false;int frames=0;
    std::uint64_t sequence{},generation{};std::vector<std::uint8_t> pixels;int width{},height{};
    auto wait_frame=[&](std::uint64_t expected) {
        if(!frame.read(pixels,sequence,width,height,generation,expected))return false;
        const auto* pixel=pixels.data()+(32*width+32)*4;
        if(width!=960 || height!=640 || pixel[0]!=224 || pixel[1]!=160 || pixel[2]!=64 || pixel[3]!=255)
            throw std::runtime_error("recovered frame pixels/viewport mismatch");
        return true;
    };
    while(GetTickCount64()-began<15000 && frames<5) {
        while(auto message=incoming.pop())if(message->value("type",std::string{})=="hello") {
            if(message->value("sessionId",std::string{})!=narrow(session))throw std::runtime_error("session mismatch");
            hello=true;channel.send({{"type","visible"},{"value",true}});
            channel.send({{"type","viewport"},{"width",960},{"height",640},{"generation",1}});
        }
        if(wait_frame(1))++frames;
        if(WaitForSingleObject(owner.get(),0)==WAIT_OBJECT_0)throw std::runtime_error("test host exited before initial frames");
        Sleep(8);
    }
    if(!hello || frames<5)throw std::runtime_error("initial capture timed out");
    Json report={{"scenario",action},{"sessionId",narrow(session)},{"initialFrames",frames},{"gameValidated",false}};
    const auto triggered=GetTickCount64();
    if(action=="capture-reset") {
        channel.send({{"type","host-selftest"},{"action",action}});
        channel.send({{"type","viewport"},{"width",960},{"height",640},{"generation",2}});
        frames=0;
        while(GetTickCount64()-triggered<5000 && frames<12) {if(wait_frame(2))++frames;Sleep(8);}
        if(frames<12)throw std::runtime_error("capture resources failed to resume after recreation");
        report["recoveredFrames"]=frames;
        report["recoveryMs"]=GetTickCount64()-triggered;
        channel.send({{"type","shutdown"}});
    } else if(action=="hidden-resize") {
        channel.send({{"type","visible"},{"value",false}});
        channel.send({{"type","viewport"},{"width",960},{"height",640},{"generation",2}});
        // 故意超过捕获超时，证明隐藏窗口不被错误当成卡死；只暂停独立测试线程。
        for(int i=0;i<56;++i) {if(WaitForSingleObject(owner.get(),100)==WAIT_OBJECT_0)throw std::runtime_error("hidden host exited unexpectedly");}
        channel.send({{"type","visible"},{"value",true}});
        frames=0;const auto shown=GetTickCount64();
        while(GetTickCount64()-shown<5000 && frames<12) {if(wait_frame(2))++frames;Sleep(8);}
        if(frames<12)throw std::runtime_error("hidden host did not resume");
        report["recoveredFrames"]=frames;channel.send({{"type","shutdown"}});
    } else if(action=="pipe-disconnect") channel.stop();
    else if(action=="host-exit") {
        if(!TerminateProcess(owner.get(),9))throw std::runtime_error("owned test host termination failed");
    } else if(action=="shutdown")channel.send({{"type","shutdown"}});
    else channel.send({{"type","host-selftest"},{"action",action}});
    if(WaitForSingleObject(owner.get(),8000)!=WAIT_OBJECT_0)throw std::runtime_error("fault did not close test host within 8s");
    DWORD code{};GetExitCodeProcess(owner.get(),&code);channel.stop();
    const bool crash=action=="renderer-crash" || action=="browser-crash";
    if((crash && code!=2) || (action=="host-exit" && code!=9) || (!crash && action!="host-exit" && code!=0))
        throw std::runtime_error("unexpected host exit code "+std::to_string(code));
    // 使用生产服务同一健康判据证明可检测；实际游戏输入释放仍须用户测试，不能由此推断通过。
    HostHealth health;health.started(began);
    report["hostHealthDetected"]=health.failure(GetTickCount64(),true,true,false)!=nullptr;
    report["exitCode"]=code;report["elapsedAfterTriggerMs"]=GetTickCount64()-triggered;
    if(crash) {
        std::ifstream input(output/L"data"/L"REFF"/L"log"/L"webview2"/L"events.jsonl");
        bool observed=false;std::string line;
        while(std::getline(input,line))if(Json::parse(line).value("event",std::string{})=="process-failed")observed=true;
        if(!observed)throw std::runtime_error("no actual WebView2 ProcessFailed event recorded");
        report["processFailedObserved"]=true;
    }
    report["passed"]=true;return report;
}
}

// 非游戏故障注入；每个故障后的下一场景都新建宿主并要求真实输出，验证重新打开不依赖旧会话。
int wmain(int argc,wchar_t** argv) {
    if(argc!=5)return 1;
    fixture_assets=std::filesystem::absolute(argv[3]);fixture_manifests=std::filesystem::absolute(argv[4]);
    const auto output=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(output);
    Json report={{"passed",false},{"gameValidated",false},{"scenarios",Json::array()}};
    try {
        const auto host=std::filesystem::absolute(argv[1]);
        for(const auto* action:{"capture-reset","hidden-resize","renderer-crash","browser-crash","pipe-disconnect","host-exit","shutdown"}) {
            report["scenarios"].push_back(scenario(host,output/action,action));
            std::cout<<action<<" passed\n";
        }
        report["passed"]=true;
    } catch(const std::exception& e) {report["error"]=e.what();std::cerr<<e.what()<<'\n';}
    std::ofstream(output/"fault-summary.json")<<report.dump(2);
    return report["passed"]==true?0:1;
}
