#pragma once

#include "launcher_types.h"

#include <string>
#include <vector>

namespace uml
{
bool ParseInjectionStage(const std::string& stageSpec, InjectionStage* stage, uint32_t* delayMs);
const char* GetStageName(InjectionStage stage);
int StageToComboIndex(InjectionStage stage);
InjectionStage ComboIndexToStage(int index);
std::vector<std::string> SplitLoadOrderList(const std::string& list);
bool ParseModEntry(const std::string& spec, ModEntry* entry);
std::string SerializeModEntry(const ModEntry& entry);
std::string SerializeLoadOrder(const std::vector<ModEntry>& mods);
}
