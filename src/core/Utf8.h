#pragma once

#include <string>
#include <string_view>

namespace yeet17::core {

std::string WideToUtf8(std::wstring_view wide);
std::wstring Utf8ToWide(std::string_view utf8);

} // namespace yeet17::core
