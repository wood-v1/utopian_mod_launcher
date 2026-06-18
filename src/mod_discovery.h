#pragma once

#include "launcher_types.h"

#include <string>
#include <vector>

namespace uml
{
std::vector<std::string> ScanModDlls();
std::vector<std::string> GetInactiveModDlls(const LauncherConfig& config);
}
