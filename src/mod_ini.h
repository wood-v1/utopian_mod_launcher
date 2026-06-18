#pragma once

#include "launcher_types.h"

#include <string>
#include <vector>

namespace uml
{
bool ParseModIniText(const std::string& text, std::vector<ModIniEntry>* entries);
std::string SerializeModIniEntries(const std::vector<ModIniEntry>& entries);
ModIniDocument LoadModIniDocument(const std::string& path);
}
