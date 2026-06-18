#pragma once

#include "launcher_types.h"

#include <string>

namespace uml
{
std::string ReadIniStringFromFile(const char* section, const char* key, const char* defaultValue, const std::string& iniPath);
bool WriteIniStringToFile(const char* section, const char* key, const std::string& value, const std::string& iniPath);
bool LoadLauncherConfig(const std::string& iniPath, LauncherConfig* config, std::string* error);
bool SaveLauncherConfig(const std::string& iniPath, const LauncherConfig& config, std::string* error);
}
