#include "frame.hpp"
#include <cstring>

namespace reff {
SharedFrame::~SharedFrame() { if (data_) UnmapViewOfFile(data_); }

// 游戏侧创建共享区，浏览器宿主只打开已存在对象；版本错误直接拒绝。
void SharedFrame::open(const std::wstring& session, bool create) {
    if (data_) { UnmapViewOfFile(data_); data_ = nullptr; }
    std::wstring mapping = L"Local\\" + session + L"-frame";
    std::wstring mutex = L"Local\\" + session + L"-lock";
    UserSecurity security;
    if (create) {
        mapping_.reset(CreateFileMappingW(INVALID_HANDLE_VALUE, security.get(), PAGE_READWRITE, 0, sizeof(FrameData), mapping.c_str()));
        if (!mapping_ || GetLastError() == ERROR_ALREADY_EXISTS) throw std::runtime_error("frame already exists");
        mutex_.reset(CreateMutexW(security.get(), FALSE, mutex.c_str()));
    } else {
        mapping_.reset(OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, mapping.c_str()));
        mutex_.reset(OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, mutex.c_str()));
    }
    if (!mapping_ || !mutex_) throw std::runtime_error("shared frame unavailable");
    data_ = static_cast<FrameData*>(MapViewOfFile(mapping_.get(), FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FrameData)));
    if (!data_) throw std::runtime_error("frame mapping failed");
    if (create) {
        std::memset(data_, 0, sizeof(FrameData));
        data_->magic = 0x46464552; data_->version = protocol_version;
        data_->width = panel_width; data_->height = panel_height; data_->viewport_generation = 1;
    }
}

// 写入完整帧再递增序号；浏览器忙或游戏正在读取时丢弃本次更新即可。
// CEF 可能因鼠标移动重复提交内容相同的整帧；像素未变时不递增序号，避免重复触发 DX12 上传。
bool SharedFrame::write(const void* bgra, int width, int height, std::uint64_t viewport_generation) {
    if (!data_ || width < 320 || height < 240 || width > frame_max_width || height > frame_max_height) return false;
    DWORD acquired = WaitForSingleObject(mutex_.get(), 0);
    if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) return false;
    const auto bytes = static_cast<size_t>(width) * height * 4;
    // 序号 0 代表从未发布或异常写入失效；即使首帧全透明，也必须发布一次供消费者初始化纹理。
    if (acquired != WAIT_ABANDONED && data_->sequence > 0 && data_->width == static_cast<std::uint32_t>(width) && data_->height == static_cast<std::uint32_t>(height) &&
        data_->viewport_generation == viewport_generation && std::memcmp(data_->pixels, bgra, bytes) == 0) {
        ReleaseMutex(mutex_.get()); return true;
    }
    std::memcpy(data_->pixels, bgra, bytes); data_->width = width; data_->height = height;
    data_->viewport_generation = viewport_generation; ++data_->sequence;
    ReleaseMutex(mutex_.get()); return true;
}

// 非阻塞快照读取；拥有互斥量后再校验元数据，避免 torn frame。
bool SharedFrame::read(std::vector<std::uint8_t>& pixels, std::uint64_t& sequence, int& width, int& height,
                       std::uint64_t& viewport_generation, std::uint64_t expected_viewport_generation) {
    if (!data_) return false;
    DWORD acquired = WaitForSingleObject(mutex_.get(), 0);
    if (acquired != WAIT_OBJECT_0 && acquired != WAIT_ABANDONED) return false;
    // 写进程异常退出可能留下半帧；清零序号，等待完整写入再恢复读取。
    if (acquired == WAIT_ABANDONED) data_->sequence = 0;
    bool valid = acquired != WAIT_ABANDONED && data_->magic == 0x46464552 && data_->version == protocol_version &&
        data_->width >= 1 && data_->height >= 1 && data_->width <= frame_max_width && data_->height <= frame_max_height && data_->sequence > sequence &&
        (expected_viewport_generation == 0 || data_->viewport_generation == expected_viewport_generation);
    if (valid) {
        width = int(data_->width); height = int(data_->height); viewport_generation = data_->viewport_generation;
        pixels.assign(data_->pixels, data_->pixels + size_t(width) * height * 4); sequence = data_->sequence;
    }
    ReleaseMutex(mutex_.get()); return valid;
}
}
