#pragma once

#include <expected>
#include <string>
#include <string_view>

namespace yeet17::tweaks {

class RestorePoint {
public:
    // description like L"YEET17PCSET: перед пакетом твиков"
    static std::expected<void, std::string> Create(std::wstring_view description);
};

} // namespace yeet17::tweaks
