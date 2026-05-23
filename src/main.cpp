#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif

#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>

namespace
{
constexpr DWORD kRemoteModulePollIntervalMs = 100;
constexpr DWORD kEngineModuleTimeoutMs = 15000;
constexpr DWORD kUiModuleTimeoutMs = 15000;
constexpr DWORD kIniBufferSize = 8192;

enum class InjectionStage
{
    Suspended = 0,
    Resume = 1,
    Engine = 2,
    Ui = 3
};

struct ModEntry
{
    std::string spec;
    std::string dllName;
    std::string dllPath;
    InjectionStage stage = InjectionStage::Resume;
    DWORD delayMs = 0;
};

std::string GetCurrentDirectoryString()
{
    char buffer[MAX_PATH] = {};
    ::GetCurrentDirectoryA(MAX_PATH, buffer);
    return buffer;
}

std::string GetModuleDirectory()
{
    char buffer[MAX_PATH] = {};
    ::GetModuleFileNameA(nullptr, buffer, MAX_PATH);

    std::string path = buffer;
    const std::size_t separator = path.find_last_of("\\/");
    if (separator == std::string::npos) {
        return ".";
    }

    return path.substr(0, separator);
}

std::string JoinPath(const std::string& left, const std::string& right)
{
    if (left.empty()) {
        return right;
    }

    if (right.empty()) {
        return left;
    }

    if (left.back() == '\\' || left.back() == '/') {
        return left + right;
    }

    return left + "\\" + right;
}

bool IsAbsolutePath(const std::string& path)
{
    return path.size() >= 2 && path[1] == ':'
        || path.size() >= 2 && path[0] == '\\' && path[1] == '\\'
        || path.size() >= 2 && path[0] == '/' && path[1] == '/';
}

std::string GetModsDirectory()
{
    return JoinPath(GetModuleDirectory(), "mods");
}

std::string GetLauncherIniPath()
{
    return JoinPath(GetModuleDirectory(), "GameModLauncher.ini");
}

std::string ResolveModsPath(const std::string& fileName)
{
    if (IsAbsolutePath(fileName)) {
        return fileName;
    }

    return JoinPath(GetModsDirectory(), fileName);
}

bool FileExists(const char* path)
{
    const DWORD attributes = ::GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool IsProcessAlive(HANDLE process)
{
    return ::WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}

bool IsFutureTick(DWORD tick, DWORD now)
{
    return tick != 0 && (LONG)(tick - now) > 0;
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

bool WaitForRemoteModule(HANDLE process, DWORD processId, const char* moduleName, DWORD timeoutMs)
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

bool WaitForStableProcess(HANDLE process, DWORD delayMs)
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

std::string Trim(const std::string& value)
{
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(begin, end - begin);
}

bool ParseDword(const std::string& value, DWORD* parsedValue)
{
    if (!parsedValue || value.empty()) {
        return false;
    }

    unsigned long result = 0;
    for (char ch : value) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }

        result = result * 10 + static_cast<unsigned long>(ch - '0');
    }

    *parsedValue = static_cast<DWORD>(result);
    return true;
}

bool ParseInjectionStage(const std::string& stageSpec, InjectionStage* stage, DWORD* delayMs)
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
    if (!delaySpec.empty() && !ParseDword(delaySpec, delayMs)) {
        return false;
    }

    return !(*stage == InjectionStage::Suspended && *delayMs != 0);
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

bool ReadLoadOrder(std::vector<ModEntry>* mods)
{
    if (!mods) {
        return false;
    }

    char buffer[kIniBufferSize] = {};
    const std::string iniPath = GetLauncherIniPath();
    ::GetPrivateProfileStringA("Mods", "LoadOrder", "", buffer, kIniBufferSize, iniPath.c_str());

    mods->clear();
    for (const std::string& spec : SplitLoadOrderList(buffer)) {
        ModEntry entry;
        if (!ParseModEntry(spec, &entry)) {
            std::printf("Invalid LoadOrder entry: %s\n", spec.c_str());
            return false;
        }

        mods->push_back(entry);
    }

    if (mods->empty()) {
        std::printf("LoadOrder is empty in %s\n", iniPath.c_str());
        return false;
    }

    return true;
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

std::string ReadIniString(const char* section, const char* key, const char* defaultValue)
{
    char buffer[kIniBufferSize] = {};
    const std::string iniPath = GetLauncherIniPath();
    ::GetPrivateProfileStringA(section, key, defaultValue, buffer, kIniBufferSize, iniPath.c_str());
    return buffer;
}
}

int main()
{
    const std::string workingDirectory = GetCurrentDirectoryString();
    const std::string gamePath = ReadIniString("General", "GamePath", "Game.exe");
    std::vector<ModEntry> mods;
    if (!ReadLoadOrder(&mods)) {
        return 1;
    }

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
        std::printf("CreateProcess failed for %s\n", gamePath.c_str());
        return 1;
    }

    bool ok = true;
    bool processResumed = false;
    InjectionStage reachedStage = InjectionStage::Suspended;

    for (const ModEntry& mod : mods) {
        if (!FileExists(mod.dllPath.c_str())) {
            std::printf("Listed DLL missing: %s\n", mod.dllPath.c_str());
            ok = false;
            break;
        }

        if (static_cast<int>(mod.stage) < static_cast<int>(reachedStage)) {
            std::printf(
                "LoadOrder cannot move backwards: %s requests %s after reaching %s\n",
                mod.spec.c_str(),
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
            std::printf("Waiting for Engine.dll before injecting %s\n", mod.dllPath.c_str());
            ok = WaitForRemoteModule(processInfo.hProcess, processInfo.dwProcessId, "Engine.dll", kEngineModuleTimeoutMs);
            if (ok) {
                reachedStage = InjectionStage::Engine;
            }
        }

        if (ok && reachedStage != InjectionStage::Ui && mod.stage == InjectionStage::Ui) {
            std::printf("Waiting for UI.dll before injecting %s\n", mod.dllPath.c_str());
            ok = WaitForRemoteModule(processInfo.hProcess, processInfo.dwProcessId, "UI.dll", kUiModuleTimeoutMs);
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
    }

    ::CloseHandle(processInfo.hThread);
    ::CloseHandle(processInfo.hProcess);
    return ok ? 0 : 1;
}
