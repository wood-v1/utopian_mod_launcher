#pragma once

#include "launcher_types.h"

#include <string>

namespace uml
{
bool ValidateLauncherConfig(const LauncherConfig& config, std::string* error);
bool LaunchGame(const LauncherConfig& config, std::string* error);
}
