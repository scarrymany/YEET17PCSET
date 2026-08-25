#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace yeet17::config {

struct Feature {
    std::string id;          // DISM FeatureName
    std::string title;       // ru
    std::string description; // ru
    bool enabled = false;
    bool available = true;
};

class Features {
public:
    Features();

    // dism /online /get-features — updates enabled/available from a real query.
    // Returns false without inventing state if DISM cannot run or exits non-zero.
    bool Refresh(std::string& error);
    [[nodiscard]] std::vector<Feature> All() const;

    // Enable/Disable via DISM (same stack as Refresh). UI confirms disable.
    bool Set(std::string_view id, bool on, std::string& error);

private:
    std::vector<Feature> features_;
};

} // namespace yeet17::config
