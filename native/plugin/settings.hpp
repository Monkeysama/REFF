#pragma once

#include "../common/protocol.hpp"
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

namespace reff {

// 持久化窗口几何：使用保存时的游戏客户区作为缩放基准，恢复后仍需按当前客户区裁剪。
struct SavedPanelGeometry {
    int client_width{};
    int client_height{};
    int left{};
    int top{};
    int width{};
    int height{};
};

// REFF Core 配置仓库：任意线程可读取或更新内存快照，磁盘写入由非渲染线程显式触发。
// 配置文件归用户所有，不属于 staging；所有外部 JSON 在进入运行时前执行白名单和范围校验。
class SettingsStore {
public:
    void open(std::filesystem::path path, std::string& warning);
    Json snapshot() const;
    bool update(const Json& patch, std::string& error);
    void reset();

    bool remember_geometry() const;
    void set_geometry(const SavedPanelGeometry& geometry);
    std::optional<SavedPanelGeometry> restored_geometry(int client_width, int client_height) const;
    bool save(std::string& error) const;

private:
    static Json defaults();
    static bool validate_document(Json& value, std::string& error);

    mutable std::mutex mutex_;
    std::filesystem::path path_;
    Json value_{defaults()};
};

} // namespace reff
