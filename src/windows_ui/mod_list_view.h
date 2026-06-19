#pragma once

#include "../launcher_services.h"
#include "../launcher_types.h"

#include <windows.h>

#include <string>
#include <vector>

namespace uml::windows_ui
{
struct InstalledFilesText
{
    std::string text;
    std::string status;
    bool found = false;
};

void AddModListColumns(HWND modList);
void PopulateModListView(HWND modList, const LauncherConfig& config, std::vector<InstalledModView>* modViews);
bool IsModViewConflicting(const LauncherConfig& config, const std::vector<ModConflictEntry>& conflicts, const InstalledModView& view);
std::string GetModConflictSummary(const LauncherConfig& config, const std::vector<ModConflictEntry>& conflicts, const InstalledModView& view);
std::string BuildModRelationshipStatus(const LauncherConfig& config, const InstalledModView& view);
std::string BuildModRelationshipDetails(const LauncherConfig& config, const InstalledModView& view);
InstalledFilesText BuildInstalledFilesText(const LauncherConfig& config, const InstalledModView& view);
}
