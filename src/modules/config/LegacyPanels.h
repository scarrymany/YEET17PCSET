#pragma once

#include <string>
#include <string_view>

namespace yeet17::config {

class LegacyPanels {
public:
    // id: control | mmsys | ncpa | appwiz | sysdm | powercfg | services | firewall
    bool Open(std::string_view id, std::string& error);
};

} // namespace yeet17::config
