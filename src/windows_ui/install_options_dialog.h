#pragma once

#include "../launcher_services.h"

#include <windows.h>

#include <string>
#include <vector>

namespace uml::windows_ui
{
struct InstallOptions
{
    bool isDllMod = false;
    std::string modName;
    std::string manifestOwner;
    std::string dllName;
};

class InstallOptionsDialog
{
public:
    InstallOptionsDialog(HWND owner, const std::string& defaultName, const std::vector<std::string>& dllNames, const std::string& defaultDllName = "");
    bool Show(InstallOptions* options);

private:
    static LRESULT CALLBACK WindowProcSetup(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WindowProcThunk(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
    void CreateControls();
    void Accept();

    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    HWND nameEdit_ = nullptr;
    HWND dllCombo_ = nullptr;
    std::string defaultName_;
    std::string defaultDllName_;
    const std::vector<std::string>& dllNames_;
    bool accepted_ = false;
    InstallOptions options_;
};

struct DllInstallDecisions
{
    std::vector<std::string> selectedDllNames;
    std::vector<std::string> overwriteDllNames;
    std::vector<std::string> skipDllNames;
    std::vector<std::string> keepSharedDllNames;
};

bool PromptDllInstallDecisions(
    HWND owner,
    const std::vector<PackageDllInstallHint>& hints,
    const std::string& primaryDllName,
    DllInstallDecisions* decisions);
}
