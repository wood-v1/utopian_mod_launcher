#include "load_order.h"

#include "path_utils.h"
#include "string_utils.h"

#include <cstring>

namespace uml
{
bool ParseInjectionStage(const std::string& stageSpec, InjectionStage* stage, uint32_t* delayMs)
{
    if (!stage || !delayMs) {
        return false;
    }

    std::string stageName = Trim(stageSpec);
    std::string delaySpec;

    const std::size_t plus = stageName.find('+');
    if (plus != std::string::npos) {
        delaySpec = Trim(stageName.substr(plus + 1));
        stageName = Trim(stageName.substr(0, plus));
    }

    if (stageName.empty()) {
        return false;
    }

    if (::_stricmp(stageName.c_str(), "suspended") == 0) {
        *stage = InjectionStage::Suspended;
    }
    else if (::_stricmp(stageName.c_str(), "resume") == 0) {
        *stage = InjectionStage::Resume;
    }
    else if (::_stricmp(stageName.c_str(), "engine") == 0) {
        *stage = InjectionStage::Engine;
    }
    else if (::_stricmp(stageName.c_str(), "ui") == 0) {
        *stage = InjectionStage::Ui;
    }
    else {
        return false;
    }

    *delayMs = 0;
    if (!delaySpec.empty() && !ParseUint32(delaySpec, delayMs)) {
        return false;
    }

    return !(*stage == InjectionStage::Suspended && *delayMs != 0);
}

const char* GetStageName(InjectionStage stage)
{
    switch (stage) {
    case InjectionStage::Suspended:
        return "suspended";
    case InjectionStage::Resume:
        return "resume";
    case InjectionStage::Engine:
        return "engine";
    case InjectionStage::Ui:
        return "ui";
    default:
        return "unknown";
    }
}

int StageToComboIndex(InjectionStage stage)
{
    return static_cast<int>(stage);
}

InjectionStage ComboIndexToStage(int index)
{
    switch (index) {
    case 0:
        return InjectionStage::Suspended;
    case 2:
        return InjectionStage::Engine;
    case 3:
        return InjectionStage::Ui;
    case 1:
    default:
        return InjectionStage::Resume;
    }
}

std::vector<std::string> SplitLoadOrderList(const std::string& list)
{
    std::vector<std::string> entries;
    std::string current;

    for (char ch : list) {
        if (ch == ',' || ch == ';' || ch == '\r' || ch == '\n') {
            const std::string trimmed = Trim(current);
            if (!trimmed.empty()) {
                entries.push_back(trimmed);
            }

            current.clear();
            continue;
        }

        current.push_back(ch);
    }

    const std::string trimmed = Trim(current);
    if (!trimmed.empty()) {
        entries.push_back(trimmed);
    }

    return entries;
}

bool ParseModEntry(const std::string& spec, ModEntry* entry)
{
    if (!entry) {
        return false;
    }

    const std::size_t at = spec.find('@');
    const std::string dllName = Trim(spec.substr(0, at));
    if (dllName.empty()) {
        return false;
    }

    entry->spec = spec;
    entry->dllName = dllName;
    entry->dllPath = ResolveModsPath(dllName);
    entry->stage = InjectionStage::Resume;
    entry->delayMs = 0;

    if (at == std::string::npos) {
        return true;
    }

    return ParseInjectionStage(spec.substr(at + 1), &entry->stage, &entry->delayMs);
}

std::string SerializeModEntry(const ModEntry& entry)
{
    std::string value = entry.dllName;
    if (entry.stage != InjectionStage::Resume || entry.delayMs != 0) {
        value += "@";
        value += GetStageName(entry.stage);
        if (entry.delayMs != 0) {
            value += "+";
            value += Uint32ToString(entry.delayMs);
        }
    }

    return value;
}

std::string SerializeLoadOrder(const std::vector<ModEntry>& mods)
{
    std::string value;
    for (std::size_t i = 0; i < mods.size(); ++i) {
        if (i != 0) {
            value += ", ";
        }

        value += SerializeModEntry(mods[i]);
    }

    return value;
}
}
