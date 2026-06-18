#pragma once

#include <windows.h>

#include <cstdint>

namespace uml
{
bool IsProcessAlive(HANDLE process);
bool WaitForRemoteModule(HANDLE process, DWORD processId, const char* moduleName, uint32_t timeoutMs);
bool WaitForStableProcess(HANDLE process, uint32_t delayMs);
}
