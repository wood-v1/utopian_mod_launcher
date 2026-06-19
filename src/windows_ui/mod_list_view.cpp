#include "mod_list_view.h"

#include "ui_helpers.h"

#include "../load_order.h"
#include "../mod_ini.h"
#include "../mod_package.h"
#include "../path_utils.h"
#include "../string_utils.h"

#include <commctrl.h>

#include <algorithm>
#include <set>
#include <sstream>

namespace uml::windows_ui
{
namespace
{
std::string GetModSettingsStatus(const ModEntry& mod)
{
    const ModIniDocument document = LoadModIniDocument(ReplaceExtension(mod.dllPath, ".ini"));
    if (!document.exists) {
        return "Missing";
    }

    if (!document.parseOk) {
        return "Raw";
    }

    return "Available";
}

std::string ToForwardSlashes(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

bool EqualsNoCase(const std::string& left, const std::string& right)
{
    return _stricmp(left.c_str(), right.c_str()) == 0;
}

std::string JoinNames(const std::vector<std::string>& values)
{
    if (values.empty()) {
        return "none";
    }

    std::string text;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            text += ", ";
        }
        text += values[i];
    }
    return text;
}

const InstalledPackageEntry* FindPackageByDll(const LauncherConfig& config, const std::string& dllName)
{
    for (const InstalledPackageEntry& package : config.packages) {
        for (const std::string& packageDll : package.dlls) {
            if (EqualsNoCase(packageDll, dllName)) {
                return &package;
            }
        }
    }
    return nullptr;
}

const InstalledPackageEntry* FindPackageById(const LauncherConfig& config, const std::string& id)
{
    for (const InstalledPackageEntry& package : config.packages) {
        if (EqualsNoCase(package.id, id)) {
            return &package;
        }
    }
    return nullptr;
}

std::string GetPackageName(const InstalledPackageEntry& package)
{
    return package.name.empty() ? package.id : package.name;
}

std::vector<std::string> GetPackageDependencyDlls(const InstalledPackageEntry& package)
{
    std::vector<std::string> dependencies;
    for (const std::string& dllName : package.dlls) {
        if (!EqualsNoCase(dllName, package.primaryDll)) {
            dependencies.push_back(dllName);
        }
    }
    return dependencies;
}

std::string BuildDllRelationshipStatus(const LauncherConfig& config, const ModEntry& mod, ModType type)
{
    if (type == ModType::SharedDll) {
        std::vector<std::string> packageNames;
        for (const SharedDllEntry& sharedDll : config.sharedDlls) {
            if (!EqualsNoCase(sharedDll.dllName, mod.dllName)) {
                continue;
            }
            for (const std::string& packageId : sharedDll.requiredBy) {
                const InstalledPackageEntry* package = FindPackageById(config, packageId);
                packageNames.push_back(package ? GetPackageName(*package) : packageId);
            }
            break;
        }
        return "Shared dependency: " + GetSharedDllDisplayName(config, mod.dllName) + ". Required by: " + JoinNames(packageNames) + ".";
    }

    const InstalledPackageEntry* package = FindPackageByDll(config, mod.dllName);
    if (!package) {
        return "Standalone DLL Mod: " + GetDllModDisplayName(mod) + ".";
    }

    if (type == ModType::DllDependency) {
        return "Dependency of package " + GetPackageName(*package) + ". Primary DLL: " + package->primaryDll + ".";
    }

    return "Primary DLL of package " + GetPackageName(*package) + ". Dependencies: " + JoinNames(GetPackageDependencyDlls(*package)) + ".";
}
}

void AddModListColumns(HWND modList)
{
    AddListViewColumn(modList, 0, "Order", 45);
    AddListViewColumn(modList, 1, "Mod", 110);
    AddListViewColumn(modList, 2, "Type", 130);
    AddListViewColumn(modList, 3, "Load stage", 78);
    AddListViewColumn(modList, 4, "Delay", 44);
    AddListViewColumn(modList, 5, "Settings", 60);
}

void PopulateModListView(HWND modList, const LauncherConfig& config, std::vector<InstalledModView>* modViews)
{
    ListView_DeleteAllItems(modList);
    if (!modViews) {
        return;
    }

    modViews->clear();
    for (std::size_t i = 0; i < config.mods.size(); ++i) {
        const ModType type = GetDllModType(config, config.mods[i].dllName);
        modViews->push_back({type, i});
        const int row = static_cast<int>(modViews->size() - 1);
        const ModEntry& mod = config.mods[i];
        const char* typeName = "DLL Mod";
        if (type == ModType::DllDependency) {
            typeName = "DLL Mod Dependency";
        }
        else if (type == ModType::SharedDll) {
            typeName = "DLL Mod Shared Dependency";
        }
        InsertListViewText(modList, row, 0, Uint32ToString(static_cast<uint32_t>(i + 1)));
        InsertListViewText(modList, row, 1, type == ModType::SharedDll ? GetSharedDllDisplayName(config, mod.dllName) : GetDllModDisplayName(mod));
        InsertListViewText(modList, row, 2, typeName);
        InsertListViewText(modList, row, 3, GetStageName(mod.stage));
        InsertListViewText(modList, row, 4, Uint32ToString(mod.delayMs));
        InsertListViewText(modList, row, 5, GetModSettingsStatus(mod));
    }

    for (std::size_t i = 0; i < config.resourceMods.size(); ++i) {
        modViews->push_back({ModType::Resource, i});
        const int row = static_cast<int>(modViews->size() - 1);
        const ResourceModEntry& mod = config.resourceMods[i];
        InsertListViewText(modList, row, 0, "#");
        InsertListViewText(modList, row, 1, GetResourceModDisplayName(mod));
        InsertListViewText(modList, row, 2, "Resource Mod");
        InsertListViewText(modList, row, 3, GetStageName(mod.stage));
        InsertListViewText(modList, row, 4, Uint32ToString(mod.delayMs));
        InsertListViewText(modList, row, 5, "N/A");
    }
}

bool IsModViewConflicting(const LauncherConfig& config, const std::vector<ModConflictEntry>& conflicts, const InstalledModView& view)
{
    const ModMatch match{view.type, view.index};
    const std::string owner = GetModManifestOwner(config, match);
    for (const ModConflictEntry& conflict : conflicts) {
        if (_stricmp(conflict.owner.c_str(), owner.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

std::string GetModConflictSummary(const LauncherConfig& config, const std::vector<ModConflictEntry>& conflicts, const InstalledModView& view)
{
    const ModMatch match{view.type, view.index};
    const std::string owner = GetModManifestOwner(config, match);
    std::set<std::string> otherMods;
    std::set<std::string> paths;
    for (const ModConflictEntry& conflict : conflicts) {
        if (_stricmp(conflict.owner.c_str(), owner.c_str()) == 0) {
            otherMods.insert(conflict.otherModName);
            paths.insert(conflict.relativePath);
        }
    }

    if (otherMods.empty()) {
        return std::string();
    }

    std::string summary = "Conflicts with ";
    std::size_t index = 0;
    for (const std::string& modName : otherMods) {
        if (index != 0) {
            summary += ", ";
        }
        summary += modName;
        ++index;
        if (index >= 3 && otherMods.size() > index) {
            summary += ", ...";
            break;
        }
    }
    summary += " on ";
    summary += Uint32ToString(static_cast<uint32_t>(paths.size()));
    summary += " file(s).";
    return summary;
}

std::string BuildModRelationshipStatus(const LauncherConfig& config, const InstalledModView& view)
{
    if (view.type == ModType::Resource) {
        const ResourceModEntry& mod = config.resourceMods[view.index];
        return "Resource Mod: " + GetResourceModDisplayName(mod) + ".";
    }

    return BuildDllRelationshipStatus(config, config.mods[view.index], view.type);
}

std::string BuildModRelationshipDetails(const LauncherConfig& config, const InstalledModView& view)
{
    std::ostringstream text;
    text << BuildModRelationshipStatus(config, view) << "\r\n";

    if (view.type == ModType::Dll || view.type == ModType::DllDependency) {
        const ModEntry& mod = config.mods[view.index];
        const InstalledPackageEntry* package = FindPackageByDll(config, mod.dllName);
        if (package) {
            text << "Package: " << GetPackageName(*package) << "\r\n";
            text << "Primary DLL: " << package->primaryDll << "\r\n";
            text << "Package DLLs: " << JoinNames(package->dlls) << "\r\n";
            text << "Shared dependencies: " << JoinNames(package->sharedDlls) << "\r\n";
            text << "Delete action: deletes this whole package; shared dependencies stay installed.\r\n";
        }
    }
    else if (view.type == ModType::SharedDll) {
        const ModEntry& mod = config.mods[view.index];
        for (const SharedDllEntry& sharedDll : config.sharedDlls) {
            if (!EqualsNoCase(sharedDll.dllName, mod.dllName)) {
                continue;
            }
            std::vector<std::string> packageNames;
            for (const std::string& packageId : sharedDll.requiredBy) {
                const InstalledPackageEntry* package = FindPackageById(config, packageId);
                packageNames.push_back(package ? GetPackageName(*package) : packageId);
            }
            text << "Required by packages: " << JoinNames(packageNames) << "\r\n";
            text << "Manifest owner: " << (sharedDll.manifestOwner.empty() ? "shared-" + sharedDll.dllName : sharedDll.manifestOwner) << "\r\n";
            text << "Delete action: blocked while required by any package.\r\n";
            break;
        }
    }

    return text.str();
}

InstalledFilesText BuildInstalledFilesText(const LauncherConfig& config, const InstalledModView& view)
{
    const ModMatch match{view.type, view.index};
    const std::string owner = GetModManifestOwner(config, match);
    const std::string modName = GetModDisplayName(config, match);
    std::vector<InstallManifestEntryInfo> entries;
    if (!ReadInstallManifestEntriesInfo(GetDefaultGameRootDirectory(), owner, &entries) || entries.empty()) {
        return {BuildModRelationshipDetails(config, view) + "\r\nNo install manifest found for " + modName + ".", "No installed file list is available for " + modName + ".", false};
    }

    std::sort(entries.begin(), entries.end(), [](const InstallManifestEntryInfo& left, const InstallManifestEntryInfo& right) {
        return _stricmp(left.relativePath.c_str(), right.relativePath.c_str()) < 0;
    });

    std::ostringstream text;
    text << BuildModRelationshipDetails(config, view) << "\r\nInstalled files:\r\n";
    for (const InstallManifestEntryInfo& entry : entries) {
        text << ToForwardSlashes(entry.relativePath) << "\r\n";
    }

    return {
        text.str(),
        "Showing " + Uint32ToString(static_cast<uint32_t>(entries.size())) + " installed file(s) for " + modName + ".",
        true};
}
}
