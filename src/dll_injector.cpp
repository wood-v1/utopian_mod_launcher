#include "dll_injector.h"

#include <cstdio>
#include <cstring>

namespace uml
{
bool InjectDll(HANDLE process, const char* dllPath)
{
    const std::size_t pathLength = std::strlen(dllPath) + 1;
    void* remoteMemory = ::VirtualAllocEx(
        process,
        nullptr,
        pathLength,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE);
    if (!remoteMemory) {
        std::printf("VirtualAllocEx failed for %s\n", dllPath);
        return false;
    }

    SIZE_T bytesWritten = 0;
    if (!::WriteProcessMemory(process, remoteMemory, dllPath, pathLength, &bytesWritten) || bytesWritten != pathLength) {
        std::printf("WriteProcessMemory failed for %s\n", dllPath);
        ::VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    HANDLE thread = ::CreateRemoteThread(
        process,
        nullptr,
        0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(::LoadLibraryA),
        remoteMemory,
        0,
        nullptr);
    if (!thread) {
        std::printf("CreateRemoteThread failed for %s\n", dllPath);
        ::VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    const DWORD waitResult = ::WaitForSingleObject(thread, INFINITE);
    if (waitResult != WAIT_OBJECT_0) {
        std::printf("WaitForSingleObject failed for %s\n", dllPath);
        ::CloseHandle(thread);
        ::VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    DWORD remoteModuleHandle = 0;
    if (!::GetExitCodeThread(thread, &remoteModuleHandle) || remoteModuleHandle == 0) {
        std::printf("LoadLibrary failed for %s\n", dllPath);
        ::CloseHandle(thread);
        ::VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
        return false;
    }

    ::CloseHandle(thread);
    ::VirtualFreeEx(process, remoteMemory, 0, MEM_RELEASE);
    return true;
}
}
