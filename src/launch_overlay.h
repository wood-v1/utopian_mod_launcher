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

struct LaunchOverlayProgress
{
    std::string current;
    uint32_t completedCount = 0;
    uint32_t totalCount = 0;
    bool finished = false;
    bool failed = false;
};

std::string GetLaunchOverlayStatusPath(uint32_t processId);
void WriteLaunchOverlayProgress(const std::string& statusPath, const LaunchOverlayProgress& progress);
void DeleteLaunchOverlayStatus(const std::string& statusPath);
void ShowLaunchOverlayForProcess(uint32_t processId, LaunchOverlayInfo info, const std::string& statusPath);
int RunLaunchOverlayProcess(uint32_t processId, LaunchOverlayInfo info, const std::string& statusPath);
}
