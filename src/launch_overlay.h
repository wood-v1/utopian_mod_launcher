#pragma once

#include <cstdint>
#include <string>

namespace uml
{
struct LaunchOverlayInfo
{
    std::string version;
    uint32_t dllModCount = 0;
    uint32_t resourceModCount = 0;
};

void ShowLaunchOverlayForProcess(uint32_t processId, LaunchOverlayInfo info);
int RunLaunchOverlayProcess(uint32_t processId, LaunchOverlayInfo info);
}
