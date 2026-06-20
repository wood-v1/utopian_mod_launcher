#include "launcher_runtime.h"

#include "dll_injector.h"
#include "launch_overlay.h"
#include "launcher_version.h"
#include "load_order.h"
#include "path_utils.h"
#include "process_utils.h"

#include <windows.h>

#include <cstdio>
#include <string>

namespace uml
{
namespace
{
std::string GetOverlayModName(const ModEntry& mod)
{
    if (!mod.name.empty()) {
        return mod.name;
    }
    if (!mod.dllName.empty()) {
        return mod.dllName;
    }
    return FileNamePart(mod.dllPath);
}

void UpdateOverlayProgress(
    const std::string& statusPath,
    const std::string& current,
    uint32_t completedCount,
    uint32_t totalCount,
    bool finished = false,
    bool failed = false)
{
    LaunchOverlayProgress progress;
    progress.current = current;
    progress.completedCount = completedCount;
    progress.totalCount = totalCount;
    progress.finished = finished;
    progress.failed = failed;
    WriteLaunchOverlayProgress(statusPath, progress);
}
}

bool ValidateLauncherConfig(const LauncherConfig& config, std::string* error)
{
    const std::string resolvedGamePath = ResolveLauncherPath(config.gamePath);
    if (!FileExists(resolvedGamePath.c_str())) {
        if (error) {
            *error = "Game executable is missing: " + resolvedGamePath;
        }
        return false;
    }

    if (config.mods.empty()) {
        return true;
    }

    InjectionStage reachedStage = InjectionStage::Suspended;
    bool needsEngineModule = false;
    bool needsUiModule = false;
    for (const ModEntry& mod : config.mods) {
        if (!FileExists(mod.dllPath.c_str())) {
            if (error) {
                *error = "Listed DLL is missing: " + mod.dllPath;
            }
            return false;
        }

        if (static_cast<int>(mod.stage) < static_cast<int>(reachedStage)) {
            if (error) {
                *error = "LoadOrder cannot move backwards: " + SerializeModEntry(mod);
            }
            return false;
        }

        if (static_cast<int>(mod.stage) > static_cast<int>(reachedStage)) {
            reachedStage = mod.stage;
        }

        if (mod.stage == InjectionStage::Engine || mod.stage == InjectionStage::Ui) {
            needsEngineModule = true;
        }
        if (mod.stage == InjectionStage::Ui) {
            needsUiModule = true;
        }
    }

    if (needsEngineModule) {
        const std::string engineModulePath = ResolveGameAdjacentPath(config.gamePath, config.engineWait.moduleName);
        if (!FileExists(engineModulePath.c_str())) {
            if (error) {
                *error = "Engine stage DLL is missing: " + engineModulePath;
            }
            return false;
        }
    }

    if (needsUiModule) {
        const std::string uiModulePath = ResolveGameAdjacentPath(config.gamePath, config.uiWait.moduleName);
        if (!FileExists(uiModulePath.c_str())) {
            if (error) {
                *error = "UI stage DLL is missing: " + uiModulePath;
            }
            return false;
        }
    }

    return true;
}

bool LaunchGame(const LauncherConfig& config, std::string* error)
{
    std::string validationError;
    if (!ValidateLauncherConfig(config, &validationError)) {
        if (error) {
            *error = validationError;
        }
        return false;
    }

    const std::string gamePath = ResolveLauncherPath(config.gamePath);
    const std::string workingDirectory = DirectoryName(gamePath);

    STARTUPINFOA startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};

    if (!::CreateProcessA(
        gamePath.c_str(),
        nullptr,
        nullptr,
        nullptr,
        FALSE,
        CREATE_SUSPENDED,
        nullptr,
        workingDirectory.c_str(),
        &startupInfo,
        &processInfo)) {
        if (error) {
            *error = "CreateProcess failed for " + gamePath;
        }
        return false;
    }

    const uint32_t totalDllMods = static_cast<uint32_t>(config.mods.size());
    const std::string overlayStatusPath = GetLaunchOverlayStatusPath(static_cast<uint32_t>(processInfo.dwProcessId));
    DeleteLaunchOverlayStatus(overlayStatusPath);

    LaunchOverlayInfo overlayInfo;
    overlayInfo.version = kLauncherVersion;
    overlayInfo.dllModCount = totalDllMods;
    overlayInfo.resourceModCount = static_cast<uint32_t>(config.resourceMods.size());
    UpdateOverlayProgress(overlayStatusPath, "Starting game...", 0, totalDllMods);
    ShowLaunchOverlayForProcess(static_cast<uint32_t>(processInfo.dwProcessId), overlayInfo, overlayStatusPath);

    bool ok = true;
    bool processResumed = false;
    InjectionStage reachedStage = InjectionStage::Suspended;
    uint32_t completedDllMods = 0;

    for (const ModEntry& mod : config.mods) {
        const std::string modName = GetOverlayModName(mod);
        if (static_cast<int>(mod.stage) < static_cast<int>(reachedStage)) {
            std::printf(
                "LoadOrder cannot move backwards: %s requests %s after reaching %s\n",
                SerializeModEntry(mod).c_str(),
                GetStageName(mod.stage),
                GetStageName(reachedStage));
            ok = false;
            break;
        }

        if (!processResumed && mod.stage != InjectionStage::Suspended) {
            UpdateOverlayProgress(overlayStatusPath, "Starting game process...", completedDllMods, totalDllMods);
            ::ResumeThread(processInfo.hThread);
            processResumed = true;
            reachedStage = InjectionStage::Resume;
        }

        if (ok && reachedStage != InjectionStage::Engine
            && reachedStage != InjectionStage::Ui
            && (mod.stage == InjectionStage::Engine || mod.stage == InjectionStage::Ui)) {
            const std::string moduleName = FileNamePart(config.engineWait.moduleName);
            UpdateOverlayProgress(
                overlayStatusPath,
                "Waiting for " + moduleName + " before " + modName,
                completedDllMods,
                totalDllMods);
            std::printf("Waiting for %s before injecting %s\n", moduleName.c_str(), mod.dllPath.c_str());
            ok = WaitForRemoteModule(
                processInfo.hProcess,
                processInfo.dwProcessId,
                moduleName.c_str(),
                config.engineWait.timeoutMs);
            if (ok) {
                reachedStage = InjectionStage::Engine;
            }
        }

        if (ok && reachedStage != InjectionStage::Ui && mod.stage == InjectionStage::Ui) {
            const std::string moduleName = FileNamePart(config.uiWait.moduleName);
            UpdateOverlayProgress(
                overlayStatusPath,
                "Waiting for " + moduleName + " before " + modName,
                completedDllMods,
                totalDllMods);
            std::printf("Waiting for %s before injecting %s\n", moduleName.c_str(), mod.dllPath.c_str());
            ok = WaitForRemoteModule(
                processInfo.hProcess,
                processInfo.dwProcessId,
                moduleName.c_str(),
                config.uiWait.timeoutMs);
            if (ok) {
                reachedStage = InjectionStage::Ui;
            }
        }

        if (ok && mod.delayMs != 0) {
            UpdateOverlayProgress(
                overlayStatusPath,
                "Waiting " + std::to_string(mod.delayMs) + " ms before " + modName,
                completedDllMods,
                totalDllMods);
            std::printf(
                "Waiting %lu ms before injecting %s\n",
                static_cast<unsigned long>(mod.delayMs),
                mod.dllPath.c_str());
            ok = WaitForStableProcess(processInfo.hProcess, mod.delayMs);
        }

        if (ok) {
            UpdateOverlayProgress(overlayStatusPath, "Injecting " + modName, completedDllMods, totalDllMods);
            std::printf("Injecting %s\n", mod.dllPath.c_str());
            ok = InjectDll(processInfo.hProcess, mod.dllPath.c_str());
            if (ok) {
                ++completedDllMods;
                UpdateOverlayProgress(overlayStatusPath, "Loaded " + modName, completedDllMods, totalDllMods);
            }
        }
    }

    if (ok && !processResumed) {
        UpdateOverlayProgress(overlayStatusPath, "Starting game process...", completedDllMods, totalDllMods);
        ::ResumeThread(processInfo.hThread);
    }

    if (ok) {
        UpdateOverlayProgress(
            overlayStatusPath,
            "Launch complete",
            completedDllMods,
            totalDllMods,
            true,
            false);
    }

    if (!ok) {
        UpdateOverlayProgress(
            overlayStatusPath,
            "Launch failed",
            completedDllMods,
            totalDllMods,
            true,
            true);
        ::TerminateProcess(processInfo.hProcess, 1);
        if (error) {
            *error = "Launch failed. See console output for details.";
        }
    }

    ::CloseHandle(processInfo.hThread);
    ::CloseHandle(processInfo.hProcess);
    return ok;
}
}
