#pragma once

#include <string>
#include <string_view>

#include "core/Utf8.h"

namespace yeet17::core {

// Resolves a resource id against Resources.resw (WinRT ResourceLoader) and
// falls back to Strings.h. Always returns Russian — we do not ship other
// languages on purpose (product requirement).
class Localization {
public:
    static Localization& Instance();

    void Initialize();
    [[nodiscard]] std::string Get(std::string_view id) const;
    [[nodiscard]] std::wstring GetWide(std::string_view id) const;

private:
    Localization() = default;
    bool ready_ = false;
};

} // namespace yeet17::core
