#pragma once

#include <string>

namespace yeet17::install {

struct Package {
    std::string id;          // winget or choco id
    std::string name;
    std::string category;    // category id (browsers, comm, ...)
    std::string source;      // "winget" | "choco"
    std::string description;
    // Optional PowerShell command executed after a successful install/upgrade
    // of this package (e.g. the SpotX mod on top of Spotify).
    std::string postInstall;
    bool custom = false;
    // UI selection state (not persisted in catalog/packages.json)
    bool selected = false;
    bool installed = false;
};

struct Category {
    std::string id;
    std::string name;
};

} // namespace yeet17::install
