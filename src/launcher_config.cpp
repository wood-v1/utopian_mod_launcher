#include "launcher_config.h"

#include "load_order.h"
#include "path_utils.h"
#include "string_utils.h"

#include <windows.h>

#include <algorithm>
#include <sstream>

namespace uml
{
namespace
{
constexpr DWORD kIniBufferSize = 8192;

uint32_t ReadIniUint32FromFile(const char* section, const char* key, uint32_t defaultValue, const std::string& iniPath)
{
    const std::string value = ReadIniStringFromFile(section, key, Uint32ToString(defaultValue).c_str(), iniPath);
    uint32_t parsed = 0;
    return ParseUint32(Trim(value), &parsed) ? parsed : defaultValue;
}

bool ReadIniBoolFromFile(const char* section, const char* key, bool defaultValue, const std::string& iniPath)
{
    const std::string value = Trim(ReadIniStringFromFile(section, key, defaultValue ? "1" : "0", iniPath));
    return value == "1" || _stricmp(value.c_str(), "true") == 0 || _stricmp(value.c_str(), "yes") == 0;
}

std::string SerializeResourceModOrder(const std::vector<ResourceModEntry>& mods)
{
    std::string value;
    for (std::size_t i = 0; i < mods.size(); ++i) {
        if (i != 0) {
            value += ", ";
        }
        value += mods[i].id;
    }
    return value;
}

std::string SerializeSharedDllNames(const std::vector<SharedDllEntry>& sharedDlls)
{
    std::string value;
    for (std::size_t i = 0; i < sharedDlls.size(); ++i) {
        if (i != 0) {
            value += ", ";
        }
        value += sharedDlls[i].dllName;
    }
    return value;
}

std::string SerializeStringList(const std::vector<std::string>& values)
{
    std::string value;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            value += ", ";
        }
        value += values[i];
    }
    return value;
}

std::string SerializePackageOrder(const std::vector<InstalledPackageEntry>& packages)
{
    std::string value;
    for (std::size_t i = 0; i < packages.size(); ++i) {
        if (i != 0) {
            value += ", ";
        }
        value += packages[i].id;
    }
    return value;
}

std::vector<std::string> SplitResourceModOrder(const std::string& order)
{
    return SplitLoadOrderList(order);
}

ModEntry* FindLoadedMod(std::vector<ModEntry>* mods, const std::string& dllName)
{
    if (!mods) {
        return nullptr;
    }
    for (ModEntry& mod : *mods) {
        if (_stricmp(mod.dllName.c_str(), dllName.c_str()) == 0) {
            return &mod;
        }
    }
    return nullptr;
}

bool ShouldMaterializeSharedDllMod(const std::string& dllName)
{
    return FileExists(ResolveModsPath(dllName).c_str());
}

bool ContainsNoCase(const std::vector<std::string>& values, const std::string& value)
{
    for (const std::string& existing : values) {
        if (_stricmp(existing.c_str(), value.c_str()) == 0) {
            return true;
        }
    }
    return false;
}

bool IsBuiltInSharedDllName(const std::string& dllName)
{
    return _stricmp(dllName.c_str(), "OynonTools.dll") == 0;
}

void RemoveMissingSharedDllMods(LauncherConfig* config)
{
    if (!config) {
        return;
    }

    std::vector<std::string> sharedDllNames;
    for (const SharedDllEntry& sharedDll : config->sharedDlls) {
        sharedDllNames.push_back(sharedDll.dllName);
    }
    for (const InstalledPackageEntry& package : config->packages) {
        for (const std::string& sharedDllName : package.sharedDlls) {
            if (!ContainsNoCase(sharedDllNames, sharedDllName)) {
                sharedDllNames.push_back(sharedDllName);
            }
        }
    }

    config->mods.erase(
        std::remove_if(config->mods.begin(), config->mods.end(), [&sharedDllNames](const ModEntry& mod) {
            const bool shared = IsBuiltInSharedDllName(mod.dllName) || ContainsNoCase(sharedDllNames, mod.dllName);
            return shared && !FileExists(ResolveModsPath(mod.dllName).c_str());
        }),
        config->mods.end());
}
}

std::string ReadIniStringFromFile(const char* section, const char* key, const char* defaultValue, const std::string& iniPath)
{
    char buffer[kIniBufferSize] = {};
    ::GetPrivateProfileStringA(section, key, defaultValue, buffer, kIniBufferSize, iniPath.c_str());
    return buffer;
}

bool WriteIniStringToFile(const char* section, const char* key, const std::string& value, const std::string& iniPath)
{
    return ::WritePrivateProfileStringA(section, key, value.c_str(), iniPath.c_str()) != FALSE;
}

bool LoadLauncherConfig(const std::string& iniPath, LauncherConfig* config, std::string* error)
{
    if (!config) {
        return false;
    }

    LauncherConfig loaded;
    loaded.gamePath = ReadIniStringFromFile("General", "GamePath", "Game.exe", iniPath);
    loaded.loggingEnabled = ReadIniBoolFromFile("Logging", "Enabled", false, iniPath);
    loaded.engineWait.moduleName = ReadIniStringFromFile("Stages", "EngineModule", "Engine.dll", iniPath);
    loaded.engineWait.timeoutMs = ReadIniUint32FromFile("Stages", "EngineTimeoutMs", kDefaultStageTimeoutMs, iniPath);
    loaded.uiWait.moduleName = ReadIniStringFromFile("Stages", "UiModule", "UI.dll", iniPath);
    loaded.uiWait.timeoutMs = ReadIniUint32FromFile("Stages", "UiTimeoutMs", kDefaultStageTimeoutMs, iniPath);

    const std::string loadOrder = ReadIniStringFromFile("Mods", "LoadOrder", "", iniPath);
    for (const std::string& spec : SplitLoadOrderList(loadOrder)) {
        ModEntry entry;
        if (!ParseModEntry(spec, &entry)) {
            if (error) {
                *error = "Invalid LoadOrder entry: " + spec;
            }
            return false;
        }

        const std::string section = "Mod:" + entry.dllName;
        entry.name = ReadIniStringFromFile(section.c_str(), "Name", entry.dllName.c_str(), iniPath);
        loaded.mods.push_back(entry);
    }

    const std::string sharedDllNames = ReadIniStringFromFile("SharedDlls", "Names", "", iniPath);
    for (const std::string& dllName : SplitLoadOrderList(sharedDllNames)) {
        const std::string trimmedDllName = Trim(dllName);
        if (trimmedDllName.empty()) {
            continue;
        }

        const std::string section = "SharedDll:" + trimmedDllName;
        SharedDllEntry entry;
        entry.dllName = trimmedDllName;
        entry.name = ReadIniStringFromFile(section.c_str(), "Name", trimmedDllName.c_str(), iniPath);
        entry.manifestOwner = ReadIniStringFromFile(section.c_str(), "Manifest", ("shared-" + trimmedDllName).c_str(), iniPath);
        entry.requiredBy = SplitLoadOrderList(ReadIniStringFromFile(section.c_str(), "RequiredBy", "", iniPath));

        const std::string stageName = ReadIniStringFromFile(section.c_str(), "Stage", "resume", iniPath);
        const std::string delayText = ReadIniStringFromFile(section.c_str(), "DelayMs", "0", iniPath);
        uint32_t parsedDelay = 0;
        if (!ParseInjectionStage(Trim(stageName), &entry.stage, &entry.delayMs)) {
            entry.stage = InjectionStage::Resume;
            entry.delayMs = 0;
        }
        if (ParseUint32(Trim(delayText), &parsedDelay)) {
            entry.delayMs = parsedDelay;
        }

        ModEntry* loadOrderEntry = FindLoadedMod(&loaded.mods, trimmedDllName);
        if (loadOrderEntry) {
            entry.stage = loadOrderEntry->stage;
            entry.delayMs = loadOrderEntry->delayMs;
            if (loadOrderEntry->name.empty() || loadOrderEntry->name == trimmedDllName) {
                loadOrderEntry->name = entry.name.empty() ? trimmedDllName : entry.name;
            }
        }
        else if (ShouldMaterializeSharedDllMod(trimmedDllName)) {
            ModEntry mod;
            mod.dllName = trimmedDllName;
            mod.dllPath = ResolveModsPath(trimmedDllName);
            mod.name = entry.name.empty() ? trimmedDllName : entry.name;
            mod.stage = entry.stage;
            mod.delayMs = entry.delayMs;
            mod.spec = SerializeModEntry(mod);
            loaded.mods.push_back(mod);
        }

        loaded.sharedDlls.push_back(entry);
    }

    const std::string packageOrder = ReadIniStringFromFile("Packages", "Order", "", iniPath);
    for (const std::string& id : SplitLoadOrderList(packageOrder)) {
        const std::string trimmedId = Trim(id);
        if (trimmedId.empty()) {
            continue;
        }

        const std::string section = "Package:" + trimmedId;
        InstalledPackageEntry entry;
        entry.id = trimmedId;
        entry.name = ReadIniStringFromFile(section.c_str(), "Name", trimmedId.c_str(), iniPath);
        entry.description = ReadIniStringFromFile(section.c_str(), "Description", "", iniPath);
        entry.manifestOwner = ReadIniStringFromFile(section.c_str(), "Manifest", trimmedId.c_str(), iniPath);
        entry.primaryDll = ReadIniStringFromFile(section.c_str(), "PrimaryDll", "", iniPath);
        entry.dlls = SplitLoadOrderList(ReadIniStringFromFile(section.c_str(), "Dlls", "", iniPath));
        entry.sharedDlls = SplitLoadOrderList(ReadIniStringFromFile(section.c_str(), "SharedDlls", "", iniPath));
        entry.filesToDelete = SplitLoadOrderList(ReadIniStringFromFile(section.c_str(), "FilesToDelete", "", iniPath));
        loaded.packages.push_back(entry);
    }

    RemoveMissingSharedDllMods(&loaded);

    const std::string resourceOrder = ReadIniStringFromFile("ResourceMods", "Order", "", iniPath);
    for (const std::string& id : SplitResourceModOrder(resourceOrder)) {
        const std::string section = "ResourceMod:" + id;
        ResourceModEntry entry;
        entry.id = id;
        entry.name = ReadIniStringFromFile(section.c_str(), "Name", id.c_str(), iniPath);
        entry.description = ReadIniStringFromFile(section.c_str(), "Description", "", iniPath);
        entry.manifestOwner = ReadIniStringFromFile(section.c_str(), "Manifest", id.c_str(), iniPath);
        entry.filesToDelete = SplitLoadOrderList(ReadIniStringFromFile(section.c_str(), "FilesToDelete", "", iniPath));
        entry.stage = InjectionStage::Resume;
        entry.delayMs = 0;

        const std::string stageName = ReadIniStringFromFile(section.c_str(), "Stage", "resume", iniPath);
        const std::string delayText = ReadIniStringFromFile(section.c_str(), "DelayMs", "0", iniPath);
        uint32_t delay = 0;
        if (!ParseUint32(Trim(delayText), &delay)) {
            delay = 0;
        }
        if (!ParseInjectionStage(Trim(stageName), &entry.stage, &entry.delayMs)) {
            entry.stage = InjectionStage::Resume;
            entry.delayMs = 0;
        }
        entry.delayMs = delay;

        loaded.resourceMods.push_back(entry);
    }

    *config = loaded;
    return true;
}

bool SaveLauncherConfig(const std::string& iniPath, const LauncherConfig& config, std::string* error)
{
    bool ok = true;
    ok = ok && WriteIniStringToFile("General", "GamePath", config.gamePath, iniPath);
    ok = ok && WriteIniStringToFile("Logging", "Enabled", config.loggingEnabled ? "1" : "0", iniPath);
    ok = ok && WriteIniStringToFile("Stages", "EngineModule", config.engineWait.moduleName, iniPath);
    ok = ok && WriteIniStringToFile("Stages", "EngineTimeoutMs", Uint32ToString(config.engineWait.timeoutMs), iniPath);
    ok = ok && WriteIniStringToFile("Stages", "UiModule", config.uiWait.moduleName, iniPath);
    ok = ok && WriteIniStringToFile("Stages", "UiTimeoutMs", Uint32ToString(config.uiWait.timeoutMs), iniPath);
    ok = ok && WriteIniStringToFile("Mods", "LoadOrder", SerializeLoadOrder(config.mods), iniPath);
    ok = ok && WriteIniStringToFile("Packages", "Order", SerializePackageOrder(config.packages), iniPath);
    ok = ok && WriteIniStringToFile("SharedDlls", "Names", SerializeSharedDllNames(config.sharedDlls), iniPath);
    ok = ok && WriteIniStringToFile("ResourceMods", "Order", SerializeResourceModOrder(config.resourceMods), iniPath);

    for (const ModEntry& mod : config.mods) {
        const std::string section = "Mod:" + mod.dllName;
        ok = ok && WriteIniStringToFile(section.c_str(), "Name", mod.name.empty() ? mod.dllName : mod.name, iniPath);
    }

    for (const SharedDllEntry& sharedDll : config.sharedDlls) {
        const std::string section = "SharedDll:" + sharedDll.dllName;
        InjectionStage stage = sharedDll.stage;
        uint32_t delayMs = sharedDll.delayMs;
        std::string name = sharedDll.name.empty() ? sharedDll.dllName : sharedDll.name;
        for (const ModEntry& mod : config.mods) {
            if (_stricmp(mod.dllName.c_str(), sharedDll.dllName.c_str()) == 0) {
                stage = mod.stage;
                delayMs = mod.delayMs;
                name = mod.name.empty() ? name : mod.name;
                break;
            }
        }
        ok = ok && WriteIniStringToFile(section.c_str(), "Name", name, iniPath);
        ok = ok && WriteIniStringToFile(section.c_str(), "Stage", GetStageName(stage), iniPath);
        ok = ok && WriteIniStringToFile(section.c_str(), "DelayMs", Uint32ToString(delayMs), iniPath);
        ok = ok && WriteIniStringToFile(section.c_str(), "Manifest", sharedDll.manifestOwner.empty() ? "shared-" + sharedDll.dllName : sharedDll.manifestOwner, iniPath);
        ok = ok && WriteIniStringToFile(section.c_str(), "RequiredBy", SerializeStringList(sharedDll.requiredBy), iniPath);
    }

    for (const InstalledPackageEntry& package : config.packages) {
        const std::string section = "Package:" + package.id;
        ok = ok && WriteIniStringToFile(section.c_str(), "Name", package.name.empty() ? package.id : package.name, iniPath);
        ok = ok && WriteIniStringToFile(section.c_str(), "Description", package.description, iniPath);
        ok = ok && WriteIniStringToFile(section.c_str(), "Manifest", package.manifestOwner.empty() ? package.id : package.manifestOwner, iniPath);
        ok = ok && WriteIniStringToFile(section.c_str(), "PrimaryDll", package.primaryDll, iniPath);
        ok = ok && WriteIniStringToFile(section.c_str(), "Dlls", SerializeStringList(package.dlls), iniPath);
        ok = ok && WriteIniStringToFile(section.c_str(), "SharedDlls", SerializeStringList(package.sharedDlls), iniPath);
        ok = ok && WriteIniStringToFile(section.c_str(), "FilesToDelete", SerializeStringList(package.filesToDelete), iniPath);
    }

    for (const ResourceModEntry& mod : config.resourceMods) {
        const std::string section = "ResourceMod:" + mod.id;
        ok = ok && WriteIniStringToFile(section.c_str(), "Name", mod.name, iniPath);
        ok = ok && WriteIniStringToFile(section.c_str(), "Description", mod.description, iniPath);
        ok = ok && WriteIniStringToFile(section.c_str(), "Stage", GetStageName(mod.stage), iniPath);
        ok = ok && WriteIniStringToFile(section.c_str(), "DelayMs", Uint32ToString(mod.delayMs), iniPath);
        ok = ok && WriteIniStringToFile(section.c_str(), "Manifest", mod.manifestOwner.empty() ? mod.id : mod.manifestOwner, iniPath);
        ok = ok && WriteIniStringToFile(section.c_str(), "FilesToDelete", SerializeStringList(mod.filesToDelete), iniPath);
    }

    if (!ok && error) {
        *error = "Failed to write " + iniPath;
    }

    return ok;
}
}
