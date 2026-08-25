#pragma once

#include "modules/install/Package.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace yeet17::install {

class PackageCatalog {
public:
    // Tries, in order: explicit path; <exeDir>/catalog/packages.json; cwd/catalog/packages.json
    bool Load();
    bool Load(const std::filesystem::path& path);

    [[nodiscard]] const std::vector<Category>& Categories() const;
    [[nodiscard]] const std::vector<Package>& All() const;
    [[nodiscard]] std::vector<Package> ByCategory(std::string_view categoryId) const;
    // Case-insensitive substring match on name, id, description
    [[nodiscard]] std::vector<Package> Search(std::string_view query) const;
    [[nodiscard]] std::optional<Package> FindById(std::string_view id) const;

    // User-added package by winget id (or choco). Persists only in-memory; Persist module owns disk.
    bool AddCustom(Package package);
    bool AddCustom(std::string_view id, std::string_view name = {});
    bool RemoveCustom(std::string_view id);

    [[nodiscard]] std::filesystem::path LoadedPath() const;

    // Extra helpers for InstallPage / Persist (in-memory only)
    void SetSelected(std::string_view id, bool selected);
    void MarkInstalled(std::string_view id, bool installed);
    [[nodiscard]] std::vector<Package> Selected() const;
    [[nodiscard]] std::vector<Package> Custom() const;
    void ImportCustomPackages(const nlohmann::json& customPackages);
    [[nodiscard]] nlohmann::json ExportCustomPackages() const;
    [[nodiscard]] bool Loaded() const;
    [[nodiscard]] std::filesystem::path LoadedFrom() const;
    [[nodiscard]] std::vector<Package> Filtered(std::string_view query) const;

private:
    bool LoadFromFile(const std::filesystem::path& path);
    Package* FindMutable(std::string_view id);

    std::vector<Category> categories_;
    std::vector<Package> packages_;
    std::filesystem::path loadedPath_;
    bool loaded_ = false;
};

} // namespace yeet17::install
