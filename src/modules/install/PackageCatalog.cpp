#include "pch.h"
#include "modules/install/PackageCatalog.h"
#include "core/Logger.h"
#include "core/Strings.h"

#ifdef _WIN32
#    include <windows.h>
#endif

namespace yeet17::install {
namespace {

const std::vector<Category> kDefaultCategories{
    {"browsers", "Браузеры"},
    {"development", "Разработка"},
    {"utilities", "Утилиты"},
    {"communication", "Общение"},
    {"media", "Медиа"},
    {"office", "Офис"},
    {"gaming", "Игры"},
};

std::filesystem::path ExecutableDirectory() {
#ifdef _WIN32
    wchar_t buf[32768]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
    if (n > 0 && n < 32768) {
        return std::filesystem::path{buf}.parent_path();
    }
#endif
    std::error_code ec;
    return std::filesystem::current_path(ec);
}

char32_t FoldCodepoint(char32_t cp) {
    if (cp >= U'A' && cp <= U'Z') {
        return cp - U'A' + U'a';
    }
    if (cp >= 0x0410 && cp <= 0x042F) { // А-Я
        return cp + 0x20;
    }
    if (cp == 0x0401) { // Ё
        return 0x0451;
    }
    return cp;
}

void AppendFolded(std::u32string& out, std::string_view utf8) {
    std::size_t i = 0;
    while (i < utf8.size()) {
        const auto c = static_cast<unsigned char>(utf8[i]);
        char32_t cp = 0;
        std::size_t n = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < utf8.size()) {
            cp = (static_cast<char32_t>(c & 0x1F) << 6)
                 | static_cast<char32_t>(static_cast<unsigned char>(utf8[i + 1]) & 0x3F);
            n = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < utf8.size()) {
            cp = (static_cast<char32_t>(c & 0x0F) << 12)
                 | (static_cast<char32_t>(static_cast<unsigned char>(utf8[i + 1]) & 0x3F) << 6)
                 | static_cast<char32_t>(static_cast<unsigned char>(utf8[i + 2]) & 0x3F);
            n = 3;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < utf8.size()) {
            cp = (static_cast<char32_t>(c & 0x07) << 18)
                 | (static_cast<char32_t>(static_cast<unsigned char>(utf8[i + 1]) & 0x3F) << 12)
                 | (static_cast<char32_t>(static_cast<unsigned char>(utf8[i + 2]) & 0x3F) << 6)
                 | static_cast<char32_t>(static_cast<unsigned char>(utf8[i + 3]) & 0x3F);
            n = 4;
        } else {
            cp = c;
        }
        out.push_back(FoldCodepoint(cp));
        i += n;
    }
}

std::u32string FoldUtf8(std::string_view utf8) {
    std::u32string out;
    out.reserve(utf8.size());
    AppendFolded(out, utf8);
    return out;
}

bool ContainsFolded(std::string_view hay, const std::u32string& needle) {
    if (needle.empty()) {
        return true;
    }
    return FoldUtf8(hay).find(needle) != std::u32string::npos;
}

bool IdsEqual(std::string_view a, std::string_view b) {
    return FoldUtf8(a) == FoldUtf8(b);
}

std::string PathUtf8(const std::filesystem::path& path) {
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

std::string NormalizeSource(std::string_view source) {
    const auto folded = FoldUtf8(source);
    if (folded == U"choco" || folded == U"chocolatey") {
        return "choco";
    }
    return "winget";
}

} // namespace

bool PackageCatalog::Load() {
    return Load(std::filesystem::path{});
}

bool PackageCatalog::Load(const std::filesystem::path& path) {
    loaded_ = false;
    packages_.clear();
    categories_.clear();
    loadedPath_.clear();

    std::vector<std::filesystem::path> candidates;
    if (!path.empty()) {
        candidates.push_back(path);
    }
    candidates.push_back(ExecutableDirectory() / "catalog" / "packages.json");
    std::error_code cwdEc;
    const auto cwd = std::filesystem::current_path(cwdEc);
    if (!cwdEc) {
        candidates.push_back(cwd / "catalog" / "packages.json");
    }

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (!std::filesystem::exists(candidate, ec) || ec) {
            continue;
        }
        return LoadFromFile(candidate);
    }

    yeet17::core::Logger::Instance().Error(std::string{yeet17::core::Strings::CatalogMissing});
    return false;
}

bool PackageCatalog::LoadFromFile(const std::filesystem::path& path) {
    std::ifstream in{path};
    if (!in) {
        yeet17::core::Logger::Instance().Error("Не удалось открыть каталог пакетов");
        return false;
    }

    std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    const auto json = nlohmann::json::parse(text, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        yeet17::core::Logger::Instance().Error("Каталог пакетов повреждён (неверный JSON)");
        return false;
    }

    if (json.contains("categories") && json["categories"].is_array()) {
        for (const auto& item : json["categories"]) {
            if (!item.is_object()) {
                continue;
            }
            Category category;
            category.id = item.value("id", std::string{});
            category.name = item.value("name", category.id);
            if (!category.id.empty()) {
                categories_.push_back(std::move(category));
            }
        }
    }
    if (categories_.empty()) {
        categories_ = kDefaultCategories;
    }

    const nlohmann::json* arr = nullptr;
    if (json.contains("packages") && json["packages"].is_array()) {
        arr = &json["packages"];
    }
    if (!arr) {
        yeet17::core::Logger::Instance().Error("Каталог пакетов не содержит списка программ");
        categories_.clear();
        return false;
    }

    for (const auto& item : *arr) {
        if (!item.is_object()) {
            continue;
        }
        Package package;
        package.id = item.value("id", item.value("wingetId", std::string{}));
        package.name = item.value("name", package.id);
        package.category = item.value("category", std::string{"utilities"});
        package.source = NormalizeSource(item.value("source", std::string{"winget"}));
        package.description = item.value("description", std::string{});
        package.custom = false; // catalog entries are never custom
        if (package.id.empty()) {
            yeet17::core::Logger::Instance().Warn("Пропущена запись каталога без идентификатора");
            continue;
        }
        if (package.name.empty()) {
            package.name = package.id;
        }
        packages_.push_back(std::move(package));
    }

    loadedPath_ = path;
    loaded_ = true;
    yeet17::core::Logger::Instance().Info(
        "Каталог пакетов: " + std::to_string(packages_.size()) + " записей (" + PathUtf8(path) + ")");
    return true;
}

const std::vector<Category>& PackageCatalog::Categories() const {
    return categories_;
}

const std::vector<Package>& PackageCatalog::All() const {
    return packages_;
}

std::vector<Package> PackageCatalog::ByCategory(std::string_view categoryId) const {
    std::vector<Package> out;
    out.reserve(packages_.size());
    for (const auto& package : packages_) {
        if (IdsEqual(package.category, categoryId)) {
            out.push_back(package);
        }
    }
    return out;
}

std::vector<Package> PackageCatalog::Search(std::string_view query) const {
    if (query.empty()) {
        return packages_;
    }
    const auto needle = FoldUtf8(query);
    std::vector<Package> out;
    out.reserve(packages_.size());
    for (const auto& package : packages_) {
        if (ContainsFolded(package.name, needle) || ContainsFolded(package.id, needle)
            || ContainsFolded(package.description, needle)) {
            out.push_back(package);
        }
    }
    return out;
}

std::optional<Package> PackageCatalog::FindById(std::string_view id) const {
    for (const auto& package : packages_) {
        if (IdsEqual(package.id, id)) {
            return package;
        }
    }
    return std::nullopt;
}

bool PackageCatalog::AddCustom(Package package) {
    if (package.id.empty() || package.id.find(' ') != std::string::npos) {
        yeet17::core::Logger::Instance().Warn("Некорректный идентификатор пакета");
        return false;
    }
    if (FindById(package.id)) {
        yeet17::core::Logger::Instance().Warn("Пакет уже есть в каталоге: " + package.id);
        return false;
    }
    package.custom = true;
    if (package.source.empty()) {
        package.source = "winget";
    } else {
        package.source = NormalizeSource(package.source);
    }
    if (package.name.empty()) {
        package.name = package.id;
    }
    if (package.category.empty()) {
        package.category = "utils";
    }
    if (package.description.empty()) {
        package.description = "Пользовательский пакет";
    }
    yeet17::core::Logger::Instance().Info("Добавлен свой пакет: " + package.id);
    packages_.push_back(std::move(package));
    return true;
}

bool PackageCatalog::AddCustom(std::string_view id, std::string_view name) {
    Package package;
    package.id = std::string{id};
    package.name = std::string{name};
    package.source = "winget";
    package.category = "utils";
    package.selected = true;
    return AddCustom(std::move(package));
}

bool PackageCatalog::RemoveCustom(std::string_view id) {
    const auto before = packages_.size();
    std::erase_if(packages_, [&](const Package& package) {
        return package.custom && IdsEqual(package.id, id);
    });
    if (packages_.size() == before) {
        return false;
    }
    yeet17::core::Logger::Instance().Info("Удалён свой пакет: " + std::string{id});
    return true;
}

std::filesystem::path PackageCatalog::LoadedPath() const {
    return loadedPath_;
}

Package* PackageCatalog::FindMutable(std::string_view id) {
    for (auto& package : packages_) {
        if (IdsEqual(package.id, id)) {
            return &package;
        }
    }
    return nullptr;
}

void PackageCatalog::SetSelected(std::string_view id, bool selected) {
    if (auto* package = FindMutable(id)) {
        package->selected = selected;
    }
}

void PackageCatalog::MarkInstalled(std::string_view id, bool installed) {
    if (auto* package = FindMutable(id)) {
        package->installed = installed;
    }
}

std::vector<Package> PackageCatalog::Selected() const {
    std::vector<Package> out;
    for (const auto& package : packages_) {
        if (package.selected) {
            out.push_back(package);
        }
    }
    return out;
}

std::vector<Package> PackageCatalog::Custom() const {
    std::vector<Package> out;
    for (const auto& package : packages_) {
        if (package.custom) {
            out.push_back(package);
        }
    }
    return out;
}

void PackageCatalog::ImportCustomPackages(const nlohmann::json& customPackages) {
    if (!customPackages.is_array()) {
        return;
    }
    for (const auto& item : customPackages) {
        if (!item.is_object()) {
            continue;
        }
        Package package;
        package.id = item.value("id", std::string{});
        package.name = item.value("name", std::string{});
        package.source = item.value("source", std::string{"winget"});
        package.category = item.value("category", std::string{"utilities"});
        package.description = item.value("description", std::string{});
        AddCustom(std::move(package));
    }
}

nlohmann::json PackageCatalog::ExportCustomPackages() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& package : packages_) {
        if (!package.custom) {
            continue;
        }
        arr.push_back({
            {"id", package.id},
            {"name", package.name},
            {"source", package.source},
            {"category", package.category},
            {"description", package.description},
        });
    }
    return arr;
}

bool PackageCatalog::Loaded() const {
    return loaded_;
}

std::filesystem::path PackageCatalog::LoadedFrom() const {
    return loadedPath_;
}

std::vector<Package> PackageCatalog::Filtered(std::string_view query) const {
    return Search(query);
}

} // namespace yeet17::install
