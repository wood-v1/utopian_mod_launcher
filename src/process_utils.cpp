#include "process_utils.h"

#include <tlhelp32.h>

#include <cstdio>

namespace uml
{
namespace
{
constexpr DWORD kRemoteModulePollIntervalMs = 100;

bool IsFutureTick(DWORD tick, DWORD now)
{
    return tick != 0 && static_cast<LONG>(tick - now) > 0;
}

bool HasRemoteModule(DWORD processId, const char* moduleName)
{
    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    MODULEENTRY32 moduleEntry = {};
    moduleEntry.dwSize = sizeof(moduleEntry);

    bool found = false;
    if (::Module32First(snapshot, &moduleEntry)) {
        do {
            if (::_stricmp(moduleEntry.szModule, moduleName) == 0) {
                found = true;
                break;
            }
        } while (::Module32Next(snapshot, &moduleEntry));
    }

    ::CloseHandle(snapshot);
    return found;
}
}

bool IsProcessAlive(HANDLE process)
{
    return ::WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}

bool WaitForRemoteModule(HANDLE process, DWORD processId, const char* moduleName, uint32_t timeoutMs)
{
    const DWORD deadline = ::GetTickCount() + timeoutMs;
    for (;;) {
        if (!IsProcessAlive(process)) {
            std::printf("Process exited before %s was loaded\n", moduleName);
            return false;
        }

        if (HasRemoteModule(processId, moduleName)) {
            return true;
        }

        if (!IsFutureTick(deadline, ::GetTickCount())) {
            break;
        }

        ::Sleep(kRemoteModulePollIntervalMs);
    }

    std::printf("Timed out waiting for %s\n", moduleName);
    return false;
}

bool WaitForStableProcess(HANDLE process, uint32_t delayMs)
{
    const DWORD deadline = ::GetTickCount() + delayMs;
    while (IsFutureTick(deadline, ::GetTickCount())) {
        if (!IsProcessAlive(process)) {
            std::printf("Process exited during stabilization delay\n");
            return false;
        }

        ::Sleep(kRemoteModulePollIntervalMs);
    }

    return true;
}
}
