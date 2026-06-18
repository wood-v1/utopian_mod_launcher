#pragma once

#include <windows.h>

#include <string>

namespace uml::windows_ui
{
class PackageTargetDialog
{
public:
    PackageTargetDialog(HWND owner, const std::string& validationError);
    bool Show(std::string* targetRelativeDirectory);

private:
    static LRESULT CALLBACK WindowProcSetup(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WindowProcThunk(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
    void CreateControls();
    void Accept();

    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    HWND targetCombo_ = nullptr;
    HWND validationEdit_ = nullptr;
    std::string validationError_;
    std::string targetRelativeDirectory_;
    bool accepted_ = false;
};
}
