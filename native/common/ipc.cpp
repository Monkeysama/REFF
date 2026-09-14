#include "ipc.hpp"
#include <sddl.h>
#include <objbase.h>
#include <array>

namespace reff {
// UTF-8 与 Windows UTF-16 转换，非法输入直接拒绝。
std::wstring widen(const std::string& value) {
    if (value.empty()) return {};
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0);
    if (!count) throw std::runtime_error("invalid UTF-8");
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), int(value.size()), result.data(), count);
    return result;
}
std::string narrow(const std::wstring& value) {
    if (value.empty()) return {};
    int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), int(value.size()), nullptr, 0, nullptr, nullptr);
    if (!count) throw std::runtime_error("invalid UTF-16");
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), int(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}

// 随机会话隔离不同游戏实例；GUID 不向网页暴露。
std::wstring make_session() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) throw std::runtime_error("GUID creation failed");
    wchar_t buffer[64]{}; StringFromGUID2(guid, buffer, 64);
    return L"REFF-" + std::to_wstring(GetCurrentProcessId()) + L"-" + buffer;
}

UserSecurity::UserSecurity() {
    HANDLE raw{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) throw std::runtime_error("token unavailable");
    Handle token(raw); DWORD size{};
    GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size);
    std::vector<char> buffer(size);
    if (!GetTokenInformation(token.get(), TokenUser, buffer.data(), size, &size)) throw std::runtime_error("token query failed");
    LPWSTR sid{};
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid, &sid)) throw std::runtime_error("SID conversion failed");
    std::wstring sddl = L"D:P(A;;GA;;;" + std::wstring(sid) + L")";
    LocalFree(sid);
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1,
            &attributes_.lpSecurityDescriptor, nullptr)) throw std::runtime_error("ACL creation failed");
}
UserSecurity::~UserSecurity() { if (attributes_.lpSecurityDescriptor) LocalFree(attributes_.lpSecurityDescriptor); }

// 有超时的 overlapped 读写只在 IPC 线程执行，取消后等待内核结束访问栈上的结构。
static bool transfer(HANDLE pipe, void* data, DWORD size, bool write) {
    DWORD done{};
    while (done < size) {
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        OVERLAPPED operation{}; operation.hEvent = event.get();
        DWORD count{};
        BOOL ok = write ? WriteFile(pipe, static_cast<char*>(data) + done, size - done, &count, &operation)
                        : ReadFile(pipe, static_cast<char*>(data) + done, size - done, &count, &operation);
        if (!ok && GetLastError() != ERROR_IO_PENDING) return false;
        if (!ok && WaitForSingleObject(event.get(), 200) != WAIT_OBJECT_0) {
            CancelIoEx(pipe, &operation); GetOverlappedResult(pipe, &operation, &count, TRUE); return false;
        }
        if (!GetOverlappedResult(pipe, &operation, &count, TRUE) || !count) return false;
        done += count;
    }
    return true;
}

Channel::~Channel() { stop(); }
void Channel::start(const std::wstring& session, bool server, Receiver receiver) {
    stop(); outgoing_.clear(); priority_outgoing_.clear();
    thread_ = std::jthread([this, session, server, receiver](std::stop_token stop) { run(stop, session, server, receiver); });
}
bool Channel::send(Json value) { return outgoing_.push(std::move(value)); }
bool Channel::send_priority(Json value) { return priority_outgoing_.push(std::move(value)); }
bool Channel::send_priority_latest(Json value) { return priority_outgoing_.replace(std::move(value)); }
void Channel::stop() { if (thread_.joinable()) { thread_.request_stop(); thread_.join(); } connected_ = false; }

// 管道断开后不自动重放业务请求；上层创建新会话后才能重连。
void Channel::run(std::stop_token stop, std::wstring session, bool server, Receiver receiver) {
    try {
        std::wstring name = L"\\\\.\\pipe\\" + session;
        Handle pipe;
        if (server) {
            UserSecurity security;
            pipe.reset(CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
                DWORD(max_message + 4), DWORD(max_message + 4), 0, security.get()));
            if (!pipe) throw std::runtime_error("pipe create failed");
            Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            OVERLAPPED operation{}; operation.hEvent = event.get();
            BOOL ok = ConnectNamedPipe(pipe.get(), &operation);
            DWORD error = ok ? ERROR_SUCCESS : GetLastError();
            if (error == ERROR_IO_PENDING) {
                while (!stop.stop_requested() && WaitForSingleObject(event.get(), 50) == WAIT_TIMEOUT) {}
                DWORD count{};
                if (stop.stop_requested()) CancelIoEx(pipe.get(), &operation);
                if (!GetOverlappedResult(pipe.get(), &operation, &count, TRUE)) return;
            } else if (error != ERROR_SUCCESS && error != ERROR_PIPE_CONNECTED) return;
        } else {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!stop.stop_requested() && std::chrono::steady_clock::now() < deadline) {
                pipe.reset(CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED, nullptr));
                if (pipe) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            if (!pipe) return;
        }
        connected_ = true;
        Decoder decoder;
        std::array<char, 65536> buffer{};
        while (!stop.stop_requested()) {
            DWORD available{};
            if (!PeekNamedPipe(pipe.get(), nullptr, 0, nullptr, &available, nullptr)) break;
            if (available) {
                DWORD count = std::min<DWORD>(available, DWORD(buffer.size()));
                if (!transfer(pipe.get(), buffer.data(), count, false)) break;
                for (auto& message : decoder.feed(buffer.data(), count)) receiver(std::move(message));
            }
            if (auto value = priority_outgoing_.pop()) {
                auto bytes = encode(*value);
                if (!transfer(pipe.get(), bytes.data(), DWORD(bytes.size()), true)) break;
            } else if (auto value = outgoing_.pop()) {
                auto bytes = encode(*value);
                if (!transfer(pipe.get(), bytes.data(), DWORD(bytes.size()), true)) break;
            } else std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    } catch (const std::exception&) { /* 连接失败由上层心跳/进程状态转为可恢复状态。 */ }
    connected_ = false;
}
}
