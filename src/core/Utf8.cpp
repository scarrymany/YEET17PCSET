#include "core/Utf8.h"

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#endif

#include <cstdint>

namespace yeet17::core {
namespace {

#ifndef _WIN32
// Minimal UTF-8 <-> UTF-16 for host-side review builds (the real app is Win32).
void AppendUtf8(std::string& out, char32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

char32_t NextCodepoint(std::string_view utf8, std::size_t& i) {
    const auto b0 = static_cast<unsigned char>(utf8[i++]);
    if (b0 < 0x80) return b0;
    int extra = 0;
    char32_t cp = 0;
    if ((b0 & 0xE0) == 0xC0) { extra = 1; cp = b0 & 0x1F; }
    else if ((b0 & 0xF0) == 0xE0) { extra = 2; cp = b0 & 0x0F; }
    else if ((b0 & 0xF8) == 0xF0) { extra = 3; cp = b0 & 0x07; }
    else return 0xFFFD;
    for (int n = 0; n < extra; ++n) {
        if (i >= utf8.size()) return 0xFFFD;
        const auto b = static_cast<unsigned char>(utf8[i++]);
        if ((b & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (b & 0x3F);
    }
    return cp;
}
#endif

} // namespace

std::wstring Utf8ToWide(std::string_view utf8) {
#ifdef _WIN32
    if (utf8.empty()) return {};
    const int needed = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), static_cast<int>(utf8.size()), out.data(), needed);
    return out;
#else
    std::wstring out;
    out.reserve(utf8.size());
    std::size_t i = 0;
    while (i < utf8.size()) {
        const char32_t cp = NextCodepoint(utf8, i);
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<wchar_t>(cp));
        } else {
            const char32_t u = cp - 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (u >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (u & 0x3FF)));
        }
    }
    return out;
#endif
}

std::string WideToUtf8(std::wstring_view wide) {
#ifdef _WIN32
    if (wide.empty()) return {};
    const int needed = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()),
        nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()),
        out.data(), needed, nullptr, nullptr);
    return out;
#else
    std::string out;
    out.reserve(wide.size());
    for (std::size_t i = 0; i < wide.size(); ++i) {
        const auto cu = static_cast<std::uint32_t>(static_cast<std::uint16_t>(wide[i]));
        char32_t cp = static_cast<char32_t>(cu);
        if (cu >= 0xD800 && cu <= 0xDBFF && i + 1 < wide.size()) {
            const auto low = static_cast<std::uint32_t>(static_cast<std::uint16_t>(wide[i + 1]));
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + (((cu - 0xD800) << 10) | (low - 0xDC00));
                ++i;
            }
        }
        AppendUtf8(out, cp);
    }
    return out;
#endif
}

} // namespace yeet17::core
