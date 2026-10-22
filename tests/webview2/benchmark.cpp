#include "frame.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>

using namespace reff;
namespace {
// 固定动态页面、尺寸及 IPC 消费频率下采样正式 WebView2 宿主；无游戏、WARP 或人工输入负载。
double percentile(std::vector<double> values,double fraction) {
    if(values.empty())return 0;std::sort(values.begin(),values.end());return values[size_t((values.size()-1)*fraction)];
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=4)return 1;
    const auto host=std::filesystem::absolute(argv[1]),assets=std::filesystem::absolute(argv[2]),output=std::filesystem::absolute(argv[3]);
    std::filesystem::create_directories(output);
    Json report={{"passed",false},{"backend","webview2"},{"gameValidated",false},{"phases",Json::array()}};
    Handle job(CreateJobObjectW(nullptr,nullptr));
    try {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!job || !SetInformationJobObject(job.get(),JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error("Job setup failed");
        const auto session=make_session();SharedFrame frame;frame.open(session,true);Queue<Json> incoming;Channel channel;
        channel.start(session,true,[&](Json message){incoming.push(std::move(message));});
        const auto cache=output/L"data"/L"REFF"/L"cache";std::filesystem::create_directories(cache);
        auto command=L"\""+host.wstring()+L"\" --reff-session="+session+L" --reff-cache=\""+cache.wstring()+L"\"";
        const auto plugins=output/L"plugins";std::filesystem::create_directories(plugins);
        command+=L" --reff-assets=\""+assets.wstring()+L"\" --reff-manifests=\""+plugins.wstring()+L"\" --reff-game=MHWILDS";
        STARTUPINFOW startup{sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        PROCESS_INFORMATION process{};
        if(!CreateProcessW(host.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))throw std::runtime_error("benchmark host launch failed");
        Handle owner(process.hProcess),thread(process.hThread);
        if(!AssignProcessToJobObject(job.get(),owner.get())) {TerminateProcess(owner.get(),1);throw std::runtime_error("Job assignment failed");}
        const auto began=GetTickCount64();ResumeThread(thread.get());thread.reset();
        bool hello=false;std::uint64_t sequence{},generation{};int width{},height{};std::vector<std::uint8_t> pixels;
        // 阶段记入同一 JSONL，资源采样器可直接据时间归组；各阶段 12 秒只用于短时测量。
        std::ofstream markers(output/"markers.jsonl");
        const auto mark=[&](const char* name) {SYSTEMTIME time{};GetSystemTime(&time);char iso[40]{};
            sprintf_s(iso,"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds);
            markers<<Json({{"phase",name},{"timeUtc",iso}}).dump()<<'\n';markers.flush();};
        mark("initializing");
        const auto read=[&](std::uint64_t expected) {
            if(!frame.read(pixels,sequence,width,height,generation,expected))return false;
            if(width!=960 || height!=640)return false;
            const auto* p=pixels.data()+(32*width+32)*4;
            return p[0]==224 && p[1]==160 && p[2]==64 && p[3]==255;
        };
        bool first=false;
        while(GetTickCount64()-began<15000 && !first) {
            while(auto message=incoming.pop())if(message->value("type",std::string{})=="hello") {
                hello=true;channel.send({{"type","visible"},{"value",true}});
                channel.send({{"type","viewport"},{"width",960},{"height",640},{"generation",1}});
            }
            first=read(1);
            if(WaitForSingleObject(owner.get(),0)==WAIT_OBJECT_0)throw std::runtime_error("benchmark host exited");Sleep(2);
        }
        if(!first || !hello)throw std::runtime_error("benchmark first frame timed out");
        report["firstFrameMs"]=GetTickCount64()-began;
        for(const auto* phase:{"shown-dynamic","hidden","reshown-dynamic"}) {
            const bool visible=std::string(phase)!="hidden";
            channel.send({{"type","visible"},{"value",visible}});mark(phase);
            const auto start=GetTickCount64();auto last=start;std::vector<double> intervals;int count=0;std::uint64_t bytes=0,first_delay=0;
            while(GetTickCount64()-start<12000) {
                if(read(1)) {const auto now=GetTickCount64();if(count)intervals.push_back(double(now-last));else first_delay=now-start;
                    last=now;++count;bytes+=pixels.size();}
                if(WaitForSingleObject(owner.get(),0)==WAIT_OBJECT_0)throw std::runtime_error("benchmark host exited in phase");Sleep(2);
            }
            report["phases"].push_back({{"phase",phase},{"frames",count},{"seconds",(GetTickCount64()-start)/1000.0},
                {"consumedBytes",bytes},{"frameIntervalMsP50",percentile(intervals,.5)},{"frameIntervalMsP95",percentile(intervals,.95)},
                {"firstFrameAfterRequestMs",first_delay}});
            if(visible && count<120)throw std::runtime_error("insufficient visible frame output");
        }
        mark("stopping");channel.send({{"type","shutdown"}});
        if(WaitForSingleObject(owner.get(),5000)!=WAIT_OBJECT_0)throw std::runtime_error("benchmark shutdown timed out");
        DWORD code{};GetExitCodeProcess(owner.get(),&code);if(code)throw std::runtime_error("benchmark exit failure");
        channel.stop();report["passed"]=true;
    } catch(const std::exception& e) {report["error"]=e.what();std::cerr<<e.what()<<'\n';}
    std::ofstream(output/"benchmark-summary.json")<<report.dump(2);std::cout<<report.dump(2)<<'\n';
    return report["passed"]==true?0:1;
}
