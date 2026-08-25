#include "pch.h"
#include "modules/tweaks/RestorePoint.h"

#ifdef _WIN32
#    if defined(__has_include)
#        if __has_include(<srrestorept.h>)
#            include <srrestorept.h>
#        else
#            include <srrestoreptapi.h>
#        endif
#    else
#        include <srrestoreptapi.h>
#    endif
#endif

namespace yeet17::tweaks {

std::expected<void, std::string> RestorePoint::Create(std::wstring_view description) {
#ifdef _WIN32
    RESTOREPOINTINFOW begin{};
    STATEMGRSTATUS status{};
    begin.dwEventType = BEGIN_SYSTEM_CHANGE;
    begin.dwRestorePtType = MODIFY_SETTINGS;
    begin.llSequenceNumber = 0;

    std::wstring desc{description};
    if (desc.size() >= std::size(begin.szDescription)) {
        desc.resize(std::size(begin.szDescription) - 1);
    }
    wcsncpy_s(begin.szDescription, desc.c_str(), _TRUNCATE);

    if (SRSetRestorePointW(&begin, &status) != TRUE) {
        spdlog::warn("SRSetRestorePointW BEGIN failed, status={}", status.nStatus);
        return std::unexpected<std::string>(
            "Не удалось создать точку восстановления (возможно, защита системы выключена)");
    }

    RESTOREPOINTINFOW end{};
    STATEMGRSTATUS endStatus{};
    end.dwEventType = END_SYSTEM_CHANGE;
    end.dwRestorePtType = MODIFY_SETTINGS;
    end.llSequenceNumber = status.llSequenceNumber;
    wcsncpy_s(end.szDescription, desc.c_str(), _TRUNCATE);
    if (SRSetRestorePointW(&end, &endStatus) != TRUE) {
        spdlog::warn("SRSetRestorePointW END failed, status={}", endStatus.nStatus);
        // BEGIN already created a usable point — treat as success.
    }

    spdlog::info("Restore point created (seq={})", status.llSequenceNumber);
    return {};
#else
    (void)description;
    return std::unexpected<std::string>("Restore points only on Windows");
#endif
}

} // namespace yeet17::tweaks
