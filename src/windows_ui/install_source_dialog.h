#pragma once

#include <windows.h>

namespace uml::windows_ui
{
class InstallSourceDialog
{
public:
    explicit InstallSourceDialog(HWND owner);
    bool Show(bool* useFolder);

private:
    static LRESULT CALLBACK WindowProcSetup(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WindowProcThunk(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
    void CreateControls();

    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    bool accepted_ = false;
    bool useFolder_ = true;
};
}
