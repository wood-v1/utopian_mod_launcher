#include "install_source_dialog.h"

#include <string>

namespace uml::windows_ui
{
namespace
{
enum
{
    IDC_SOURCE_FOLDER = 4101,
    IDC_SOURCE_ARCHIVE
};
}

InstallSourceDialog::InstallSourceDialog(HWND owner)
    : owner_(owner)
{
}

bool InstallSourceDialog::Show(bool* useFolder)
{
    WNDCLASSA windowClass = {};
    windowClass.lpfnWndProc = &InstallSourceDialog::WindowProcSetup;
    windowClass.hInstance = ::GetModuleHandleA(nullptr);
    windowClass.lpszClassName = "UtopianInstallSourceDialog";
    windowClass.hCursor = ::LoadCursorA(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    ::RegisterClassA(&windowClass);

    RECT parentRect = {};
    ::GetWindowRect(owner_, &parentRect);
    const int width = 360;
    const int height = 170;
    const int x = parentRect.left + ((parentRect.right - parentRect.left) - width) / 2;
    const int y = parentRect.top + ((parentRect.bottom - parentRect.top) - height) / 2;

    window_ = ::CreateWindowExA(
        WS_EX_DLGMODALFRAME,
        windowClass.lpszClassName,
        "Install Mod",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x,
        y,
        width,
        height,
        owner_,
        nullptr,
        ::GetModuleHandleA(nullptr),
        this);
    if (!window_) {
        ::MessageBoxA(owner_, ("Failed to open install source dialog. Win32 error: " + std::to_string(::GetLastError())).c_str(), "Install Mod", MB_ICONERROR | MB_OK);
        return false;
    }

    ::EnableWindow(owner_, FALSE);
    ::ShowWindow(window_, SW_SHOW);
    ::SetForegroundWindow(window_);
    ::UpdateWindow(window_);

    MSG message = {};
    while (::IsWindow(window_) && ::GetMessageA(&message, nullptr, 0, 0) > 0) {
        if (!::IsDialogMessageA(window_, &message)) {
            ::TranslateMessage(&message);
            ::DispatchMessageA(&message);
        }
    }

    ::EnableWindow(owner_, TRUE);
    ::SetActiveWindow(owner_);
    if (accepted_ && useFolder) {
        *useFolder = useFolder_;
    }
    return accepted_;
}

LRESULT CALLBACK InstallSourceDialog::WindowProcSetup(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        CREATESTRUCTA* createStruct = reinterpret_cast<CREATESTRUCTA*>(lParam);
        InstallSourceDialog* self = reinterpret_cast<InstallSourceDialog*>(createStruct->lpCreateParams);
        ::SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        ::SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&InstallSourceDialog::WindowProcThunk));
        self->window_ = window;
        return self->WindowProc(message, wParam, lParam);
    }

    return ::DefWindowProcA(window, message, wParam, lParam);
}

LRESULT CALLBACK InstallSourceDialog::WindowProcThunk(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    InstallSourceDialog* self = reinterpret_cast<InstallSourceDialog*>(::GetWindowLongPtrA(window, GWLP_USERDATA));
    if (!self) {
        return ::DefWindowProcA(window, message, wParam, lParam);
    }

    return self->WindowProc(message, wParam, lParam);
}

LRESULT InstallSourceDialog::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        CreateControls();
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_SOURCE_FOLDER) {
            useFolder_ = true;
            accepted_ = true;
            ::DestroyWindow(window_);
            return 0;
        }
        if (LOWORD(wParam) == IDC_SOURCE_ARCHIVE) {
            useFolder_ = false;
            accepted_ = true;
            ::DestroyWindow(window_);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            ::DestroyWindow(window_);
            return 0;
        }
        break;
    case WM_CLOSE:
        ::DestroyWindow(window_);
        return 0;
    default:
        break;
    }
    return ::DefWindowProcA(window_, message, wParam, lParam);
}

void InstallSourceDialog::CreateControls()
{
    HFONT font = reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
    ::CreateWindowExA(0, "STATIC", "Choose mod package source:", WS_CHILD | WS_VISIBLE, 18, 18, 250, 20, window_, nullptr, ::GetModuleHandleA(nullptr), nullptr);
    HWND folder = ::CreateWindowExA(0, "BUTTON", "Folder", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 18, 58, 95, 32, window_, reinterpret_cast<HMENU>(IDC_SOURCE_FOLDER), ::GetModuleHandleA(nullptr), nullptr);
    HWND archive = ::CreateWindowExA(0, "BUTTON", "Archive", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 126, 58, 95, 32, window_, reinterpret_cast<HMENU>(IDC_SOURCE_ARCHIVE), ::GetModuleHandleA(nullptr), nullptr);
    HWND cancel = ::CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 238, 58, 80, 32, window_, reinterpret_cast<HMENU>(IDCANCEL), ::GetModuleHandleA(nullptr), nullptr);

    HWND child = ::GetWindow(window_, GW_CHILD);
    while (child) {
        ::SendMessageA(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        child = ::GetWindow(child, GW_HWNDNEXT);
    }
    ::SendMessageA(folder, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    ::SendMessageA(archive, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    ::SendMessageA(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}
}
