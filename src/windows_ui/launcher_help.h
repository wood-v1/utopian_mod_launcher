#pragma once

#include "../launcher_services.h"
#include "../launcher_types.h"

#include <windows.h>

#include <vector>

namespace uml::windows_ui
{
void ShowLauncherHelpDialog(HWND owner);
void ShowBackedUpFilesDialog(HWND owner, const LauncherConfig& config);
void ShowBackedUpFilesAuditDialog(HWND owner, const std::vector<ManifestAuditEntry>& audit);
bool ConfirmPackageConflictsDialog(HWND owner, const std::vector<ModConflictEntry>& conflicts);
}
