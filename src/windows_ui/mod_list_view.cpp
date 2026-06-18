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
}

void AddModListColumns(HWND modList)
{
    AddListViewColumn(modList, 0, "Order", 45);
    AddListViewColumn(modList, 1, "Mod", 125);
    AddListViewColumn(modList, 2, "Type", 88);
    AddListViewColumn(modList, 3, "Load stage", 82);
    AddListViewColumn(modList, 4, "Delay", 52);
    AddListViewColumn(modList, 5, "Settings", 70);
}

void PopulateModListView(HWND modList, const LauncherConfig& config, std::vector<InstalledModView>* modViews)
{
    ListView_DeleteAllItems(modList);
    if (!modViews) {
        return;
    }

    modViews->clear();
    for (std::size_t i = 0; i < config.mods.size(); ++i) {
        modViews->push_back({ModType::Dll, i});
        const int row = static_cast<int>(modViews->size() - 1);
        const ModEntry& mod = config.mods[i];
        InsertListViewText(modList, row, 0, Uint32ToString(static_cast<uint32_t>(i + 1)));
        InsertListViewText(modList, row, 1, GetDllModDisplayName(mod));
        InsertListViewText(modList, row, 2, "DLL Mod");
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

InstalledFilesText BuildInstalledFilesText(const LauncherConfig& config, const InstalledModView& view)
{
    const ModMatch match{view.type, view.index};
    const std::string owner = GetModManifestOwner(config, match);
    const std::string modName = GetModDisplayName(config, match);
    std::vector<InstallManifestEntryInfo> entries;
    if (!ReadInstallManifestEntriesInfo(GetDefaultGameRootDirectory(), owner, &entries) || entries.empty()) {
        return {"No install manifest found for " + modName + ".", "No installed file list is available for " + modName + ".", false};
    }

    std::sort(entries.begin(), entries.end(), [](const InstallManifestEntryInfo& left, const InstallManifestEntryInfo& right) {
        return _stricmp(left.relativePath.c_str(), right.relativePath.c_str()) < 0;
    });

    std::ostringstream text;
    for (const InstallManifestEntryInfo& entry : entries) {
        text << ToForwardSlashes(entry.relativePath) << "\r\n";
    }

    return {
        text.str(),
        "Showing " + Uint32ToString(static_cast<uint32_t>(entries.size())) + " installed file(s) for " + modName + ".",
        true};
}
}
