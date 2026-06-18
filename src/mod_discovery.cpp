#include "mod_discovery.h"

#include "path_utils.h"
#include "string_utils.h"

#include <windows.h>

#include <algorithm>
#include <set>

namespace uml
{
std::vector<std::string> ScanModDlls()
{
    std::vector<std::string> result;
    const std::string modsDirectory = GetModsDirectory();
    const std::string pattern = JoinPath(modsDirectory, "*.dll");

    WIN32_FIND_DATAA findData = {};
    HANDLE findHandle = ::FindFirstFileA(pattern.c_str(), &findData);
    if (findHandle == INVALID_HANDLE_VALUE) {
        return result;
    }

    do {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            result.push_back(findData.cFileName);
        }
    } while (::FindNextFileA(findHandle, &findData));

    ::FindClose(findHandle);
    std::sort(result.begin(), result.end(), [](const std::string& left, const std::string& right) {
        return _stricmp(left.c_str(), right.c_str()) < 0;
    });
    return result;
}

std::vector<std::string> GetInactiveModDlls(const LauncherConfig& config)
{
    std::set<std::string> active;
    for (const ModEntry& mod : config.mods) {
        active.insert(ToLower(mod.dllName));
    }

    std::vector<std::string> inactive;
    for (const std::string& dllName : ScanModDlls()) {
        if (active.find(ToLower(dllName)) == active.end()) {
            inactive.push_back(dllName);
        }
    }

    return inactive;
}
}
