#include "launcher_config.h"

#include "load_order.h"
#include "string_utils.h"

#include <windows.h>

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

std::vector<std::string> SplitResourceModOrder(const std::string& order)
{
    return SplitLoadOrderList(order);
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

    const std::string resourceOrder = ReadIniStringFromFile("ResourceMods", "Order", "", iniPath);
    for (const std::string& id : SplitResourceModOrder(resourceOrder)) {
        const std::string section = "ResourceMod:" + id;
        ResourceModEntry entry;
        entry.id = id;
        entry.name = ReadIniStringFromFile(section.c_str(), "Name", id.c_str(), iniPath);
        entry.manifestOwner = ReadIniStringFromFile(section.c_str(), "Manifest", id.c_str(), iniPath);
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
    ok = ok && WriteIniStringToFile("ResourceMods", "Order", SerializeResourceModOrder(config.resourceMods), iniPath);

    for (const ModEntry& mod : config.mods) {
        const std::string section = "Mod:" + mod.dllName;
        ok = ok && WriteIniStringToFile(section.c_str(), "Name", mod.name.empty() ? mod.dllName : mod.name, iniPath);
    }

    for (const ResourceModEntry& mod : config.resourceMods) {
        const std::string section = "ResourceMod:" + mod.id;
        ok = ok && WriteIniStringToFile(section.c_str(), "Name", mod.name, iniPath);
        ok = ok && WriteIniStringToFile(section.c_str(), "Stage", GetStageName(mod.stage), iniPath);
        ok = ok && WriteIniStringToFile(section.c_str(), "DelayMs", Uint32ToString(mod.delayMs), iniPath);
        ok = ok && WriteIniStringToFile(section.c_str(), "Manifest", mod.manifestOwner.empty() ? mod.id : mod.manifestOwner, iniPath);
    }

    if (!ok && error) {
        *error = "Failed to write " + iniPath;
    }

    return ok;
}
}
