#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace uml
{
constexpr uint32_t kDefaultStageTimeoutMs = 15000;

enum class InjectionStage
{
    Suspended = 0,
    Resume = 1,
    Engine = 2,
    Ui = 3
};

enum class ModType
{
    Dll,
    DllDependency,
    SharedDll,
    Resource
};

struct StageWait
{
    std::string moduleName;
    uint32_t timeoutMs = kDefaultStageTimeoutMs;
};

struct ModEntry
{
    std::string spec;
    std::string name;
    std::string dllName;
    std::string dllPath;
    InjectionStage stage = InjectionStage::Resume;
    uint32_t delayMs = 0;
};

struct ResourceModEntry
{
    std::string id;
    std::string name;
    std::string manifestOwner;
    InjectionStage stage = InjectionStage::Resume;
    uint32_t delayMs = 0;
};

struct SharedDllEntry
{
    std::string dllName;
    std::string name;
    std::string manifestOwner;
    std::vector<std::string> requiredBy;
    InjectionStage stage = InjectionStage::Resume;
    uint32_t delayMs = 0;
};

struct InstalledPackageEntry
{
    std::string id;
    std::string name;
    std::string manifestOwner;
    std::string primaryDll;
    std::vector<std::string> dlls;
    std::vector<std::string> sharedDlls;
};

struct InstalledModView
{
    ModType type = ModType::Dll;
    std::size_t index = 0;
};

struct LauncherConfig
{
    std::string gamePath = "Game.exe";
    bool loggingEnabled = false;
    StageWait engineWait = {"Engine.dll", kDefaultStageTimeoutMs};
    StageWait uiWait = {"UI.dll", kDefaultStageTimeoutMs};
    std::vector<ModEntry> mods;
    std::vector<InstalledPackageEntry> packages;
    std::vector<SharedDllEntry> sharedDlls;
    std::vector<ResourceModEntry> resourceMods;
};

struct ModIniEntry
{
    std::string section;
    std::string key;
    std::string value;
};

struct ModIniDocument
{
    std::string path;
    bool exists = false;
    bool parseOk = false;
    std::string rawText;
    std::vector<ModIniEntry> entries;
};
}
