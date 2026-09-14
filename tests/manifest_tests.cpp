#include "manifest.hpp"
#include "lifecycle.hpp"
#include <fstream>
#include <iostream>

// 验证清单边界、重复 ID、游戏过滤和会话关闭屏障；测试不启动 CEF 或游戏进程。
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        std::vector<std::string> errors;
        auto manifests = reff::scan_manifests(argv[1], errors);
        if (manifests.size() != 2 || errors.size() != 1 || errors[0].find("保留命名空间") == std::string::npos)
            throw std::runtime_error("manifest scan or reserved namespace mismatch");
        if (manifests[0].id != "example.settings" || manifests[1].id != "example.status") throw std::runtime_error("manifest sort mismatch");
        auto summary = reff::manifest_summary(manifests[0]);
        if (summary.contains("root") || summary.value("entry", "") != "ui/index.html" ||
            summary.value("url", "") != "reff://plugin/example.settings/ui/index.html" ||
            summary.value("mode", "") != "component") throw std::runtime_error("summary leaked path or UI mode");
        if (summary["schema"].value("load", "") != "example.settings.get" ||
            summary["schema"].value("changeEvent", "") != "example.settings.changed")
            throw std::runtime_error("schema state contract mismatch");
        if (manifests[1].ui_mode != "isolated-page") throw std::runtime_error("page mode fallback mismatch");

        reff::SessionGate gate;
        auto epoch = gate.open(); gate.set_connected(true);
        if (!gate.accepts(epoch) || !gate.connected() || gate.begin_close() == false || gate.accepts(epoch) || gate.begin_close())
            throw std::runtime_error("session gate mismatch");
        reff::RestartBudget budget(2);
        if (!budget.consume() || !budget.consume() || budget.consume() || budget.used() != 2) throw std::runtime_error("restart budget mismatch");
        std::cout << "manifest and lifecycle checks passed\n";
        return 0;
    } catch (const std::exception& exception) { std::cerr << exception.what() << '\n'; return 1; }
}
