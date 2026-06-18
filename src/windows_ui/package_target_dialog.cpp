#include "package_target_dialog.h"

#include <windowsx.h>

#include <vector>

namespace uml::windows_ui
{
namespace
{
std::string NormalizeNewlines(std::string text)
{
    std::string normalized;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            normalized += "\r\n";
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            continue;
        }
        if (text[i] == '\n') {
            normalized += "\r\n";
            continue;
        }
        normalized.push_back(text[i]);
    }
    return normalized;
}

const std::vector<std::string>& ResourceInstallTargets()
{
    static const std::vector<std::string> targets = {
        "data",
        "data\\Actors",
        "data\\Scripts",
        "data\\Sounds",
        "data\\Textures",
        "data\\UI",
        "data\\Video",
        "data\\Scenes",
        "data\\Geometries",
        "data\\World"
    };
    return targets;
}
}

PackageTargetDialog::PackageTargetDialog(HWND owner, const std::string& validationError)
    : owner_(owner)
    , validationError_(NormalizeNewlines(validationError))
{
}

bool PackageTargetDialog::Show(std::string* targetRelativeDirectory)
{
    WNDCLASSA windowClass = {};
    windowClass.lpfnWndProc = &PackageTargetDialog::WindowProcSetup;
    windowClass.hInstance = ::GetModuleHandleA(nullptr);
    windowClass.lpszClassName = "UtopianPackageTargetDialog";
    windowClass.hCursor = ::LoadCursorA(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    ::RegisterClassA(&windowClass);

    RECT parentRect = {};
    ::GetWindowRect(owner_, &parentRect);
    const int width = 560;
    const int height = 320;
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
        ::MessageBoxA(owner_, ("Failed to open package target dialog. Win32 error: " + std::to_string(::GetLastError())).c_str(), "Install Mod", MB_ICONERROR | MB_OK);
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
    if (accepted_ && targetRelativeDirectory) {
        *targetRelativeDirectory = targetRelativeDirectory_;
    }
    return accepted_;
}

LRESULT CALLBACK PackageTargetDialog::WindowProcSetup(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        CREATESTRUCTA* createStruct = reinterpret_cast<CREATESTRUCTA*>(lParam);
        PackageTargetDialog* self = reinterpret_cast<PackageTargetDialog*>(createStruct->lpCreateParams);
        ::SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        ::SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&PackageTargetDialog::WindowProcThunk));
        self->window_ = window;
        return self->WindowProc(message, wParam, lParam);
    }

    return ::DefWindowProcA(window, message, wParam, lParam);
}

LRESULT CALLBACK PackageTargetDialog::WindowProcThunk(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    PackageTargetDialog* self = reinterpret_cast<PackageTargetDialog*>(::GetWindowLongPtrA(window, GWLP_USERDATA));
    if (!self) {
        return ::DefWindowProcA(window, message, wParam, lParam);
    }

    return self->WindowProc(message, wParam, lParam);
}

LRESULT PackageTargetDialog::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        CreateControls();
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            Accept();
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

void PackageTargetDialog::CreateControls()
{
    HFONT font = reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
    ::CreateWindowExA(
        0,
        "STATIC",
        "This package does not use game-root layout. Choose where its files should be installed:",
        WS_CHILD | WS_VISIBLE,
        16,
        16,
        520,
        36,
        window_,
        nullptr,
        ::GetModuleHandleA(nullptr),
        nullptr);

    targetCombo_ = ::CreateWindowExA(
        0,
        "COMBOBOX",
        "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        16,
        66,
        520,
        160,
        window_,
        nullptr,
        ::GetModuleHandleA(nullptr),
        nullptr);
    for (const std::string& target : ResourceInstallTargets()) {
        ComboBox_AddString(targetCombo_, target.c_str());
    }
    ComboBox_SetCurSel(targetCombo_, 0);

    validationEdit_ = ::CreateWindowExA(
        WS_EX_CLIENTEDGE,
        "EDIT",
        validationError_.empty() ? "" : validationError_.c_str(),
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        16,
        106,
        520,
        108,
        window_,
        nullptr,
        ::GetModuleHandleA(nullptr),
        nullptr);

    HWND ok = ::CreateWindowExA(0, "BUTTON", "Continue", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 356, 232, 84, 30, window_, reinterpret_cast<HMENU>(IDOK), ::GetModuleHandleA(nullptr), nullptr);
    HWND cancel = ::CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 452, 232, 84, 30, window_, reinterpret_cast<HMENU>(IDCANCEL), ::GetModuleHandleA(nullptr), nullptr);

    HWND child = ::GetWindow(window_, GW_CHILD);
    while (child) {
        ::SendMessageA(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        child = ::GetWindow(child, GW_HWNDNEXT);
    }
    ::SendMessageA(ok, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    ::SendMessageA(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void PackageTargetDialog::Accept()
{
    const int index = ComboBox_GetCurSel(targetCombo_);
    if (index == CB_ERR || index >= static_cast<int>(ResourceInstallTargets().size())) {
        return;
    }
    targetRelativeDirectory_ = ResourceInstallTargets()[static_cast<std::size_t>(index)];
    accepted_ = true;
    ::DestroyWindow(window_);
}
}
