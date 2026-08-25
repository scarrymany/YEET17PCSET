#pragma once

// Precompiled header: keep this to truly stable, widely used headers.
// WinRT / Win32 stay behind _WIN32 so the tree remains readable on Linux.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#ifdef _WIN32
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <windows.h>
#    include <shlobj.h>
#    include <shellapi.h>
// windows.h defines GetCurrentTime as a macro; it collides with
// Storyboard::GetCurrentTime in winrt/Microsoft.UI.Xaml.Media.Animation.h.
#    ifdef GetCurrentTime
#        undef GetCurrentTime
#    endif
#    include <winrt/base.h>
#    include <winrt/Windows.Foundation.h>
#    include <winrt/Windows.Foundation.Collections.h>
#    include <winrt/Microsoft.UI.Xaml.h>
#    include <winrt/Microsoft.UI.Xaml.Controls.h>
#    include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#    include <winrt/Microsoft.UI.Xaml.Media.h>
#    include <winrt/Microsoft.UI.Xaml.Navigation.h>
// Our own merged WinRT projection. Shared by every target (the exe and every static lib
// module), so it must not drag in anything outside their common include paths - notably not
// the concrete <Class>.xaml.h headers, which need the exe-only XAML-generated include dirs.
// See pch_exe.h for those.
#    include <winrt/yeet17.h>
#endif
