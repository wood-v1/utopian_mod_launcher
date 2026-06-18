#include "launcher_runtime.h"

#include "dll_injector.h"
#include "load_order.h"
#include "path_utils.h"
#include "process_utils.h"

#include <windows.h>

#include <cstdio>

namespace uml
{
bool ValidateLauncherConfig(const LauncherConfig& config, std::string* error)
{
    const std::string resolvedGamePath = ResolveLauncherPath(config.gamePath);
    if (!FileExists(resolvedGamePath.c_str())) {
        if (error) {
            *error = "Game executable is missing: " + resolvedGamePath;
        }
        return false;
    }

    const std::string engineModulePath = ResolveGameAdjacentPath(config.gamePath, config.engineWait.moduleName);
    if (!FileExists(engineModulePath.c_str())) {
        if (error) {
            *error = "Engine stage DLL is missing: " + engineModulePath;
        }
        return false;
    }

    const std::string uiModulePath = ResolveGameAdjacentPath(config.gamePath, config.uiWait.moduleName);
    if (!FileExists(uiModulePath.c_str())) {
        if (error) {
            *error = "UI stage DLL is missing: " + uiModulePath;
        }
        return false;
    }

    if (config.mods.empty()) {
        if (error) {
            *error = "LoadOrder is empty.";
        }
        return false;
    }

    InjectionStage reachedStage = InjectionStage::Suspended;
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

    bool ok = true;
    bool processResumed = false;
    InjectionStage reachedStage = InjectionStage::Suspended;

    for (const ModEntry& mod : config.mods) {
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
            ::ResumeThread(processInfo.hThread);
            processResumed = true;
            reachedStage = InjectionStage::Resume;
        }

        if (ok && reachedStage != InjectionStage::Engine
            && reachedStage != InjectionStage::Ui
            && (mod.stage == InjectionStage::Engine || mod.stage == InjectionStage::Ui)) {
            const std::string moduleName = FileNamePart(config.engineWait.moduleName);
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
            std::printf(
                "Waiting %lu ms before injecting %s\n",
                static_cast<unsigned long>(mod.delayMs),
                mod.dllPath.c_str());
            ok = WaitForStableProcess(processInfo.hProcess, mod.delayMs);
        }

        if (ok) {
            std::printf("Injecting %s\n", mod.dllPath.c_str());
            ok = InjectDll(processInfo.hProcess, mod.dllPath.c_str());
        }
    }

    if (ok && !processResumed) {
        ::ResumeThread(processInfo.hThread);
    }

    if (!ok) {
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
