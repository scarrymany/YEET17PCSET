#pragma once

#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace yeet17::tweaks {

enum class Tier { Essential, Advanced };
enum class Hive { Hkcu, Hklm };
enum class RegType { Dword, Sz };
enum class StartType { Disabled, Manual, Auto };

struct RegistryOp {
    Hive hive{};
    std::string key;
    std::string name;
    RegType valueType{RegType::Dword};
    nlohmann::json apply;
    bool createKey{true};
};

struct ServiceOp {
    std::string name;
    StartType applyStartType{StartType::Disabled};
    bool stopOnApply{true};
};

struct TaskOp {
    std::string path;
    std::string apply;
};

using Op = std::variant<RegistryOp, ServiceOp, TaskOp>;

struct Tweak {
    std::string id;
    std::string title;
    std::string description;
    std::string category;
    Tier tier{Tier::Essential};
    std::vector<std::string> os;
    bool requiresConfirm{false};
    bool reversible{true};
    bool rebootRecommended{false};
    bool explorerRestart{false};
    std::vector<std::string> presets;
    std::vector<Op> ops;
};

} // namespace yeet17::tweaks
