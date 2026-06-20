#include "launcher_help.h"

#include "ui_helpers.h"

#include "../mod_package.h"

#include <sstream>
#include <string>
#include <utility>

namespace uml::windows_ui
{
namespace
{
class ScrollableTextDialog
{
public:
    ScrollableTextDialog(HWND owner, const char* title, std::string text)
        : owner_(owner), title_(title), text_(std::move(text))
    {
    }

    void Show()
    {
        const HINSTANCE instance = ::GetModuleHandleA(nullptr);
        RegisterWindowClass(instance);

        const int width = 760;
        const int height = 540;
        int x = CW_USEDEFAULT;
        int y = CW_USEDEFAULT;
        if (owner_) {
            RECT ownerRect = {};
            ::GetWindowRect(owner_, &ownerRect);
            x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
            y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
            if (x < 0) {
                x = 0;
            }
            if (y < 0) {
                y = 0;
            }
        }

        window_ = ::CreateWindowExA(
            WS_EX_DLGMODALFRAME,
            ClassName(),
            title_,
            WS_CAPTION | WS_SYSMENU | WS_POPUP,
            x,
            y,
            width,
            height,
            owner_,
            nullptr,
            instance,
            this);
        if (!window_) {
            ShowError(owner_, "Failed to open backups dialog.");
            return;
        }

        ::EnableWindow(owner_, FALSE);
        ::ShowWindow(window_, SW_SHOW);
        ::UpdateWindow(window_);

        MSG message = {};
        while (!done_ && ::GetMessageA(&message, nullptr, 0, 0) > 0) {
            if (!::IsDialogMessageA(window_, &message)) {
                ::TranslateMessage(&message);
                ::DispatchMessageA(&message);
            }
        }

        ::EnableWindow(owner_, TRUE);
        ::DestroyWindow(window_);
        if (owner_) {
            ::SetActiveWindow(owner_);
        }
    }

private:
    static const char* ClassName()
    {
        return "UtopianScrollableTextDialog";
    }

    static void RegisterWindowClass(HINSTANCE instance)
    {
        static bool registered = false;
        if (registered) {
            return;
        }

        WNDCLASSA wc = {};
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = ClassName();
        wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        ::RegisterClassA(&wc);
        registered = true;
    }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        ScrollableTextDialog* dialog = nullptr;
        if (message == WM_NCCREATE) {
            CREATESTRUCTA* create = reinterpret_cast<CREATESTRUCTA*>(lParam);
            dialog = static_cast<ScrollableTextDialog*>(create->lpCreateParams);
            ::SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dialog));
            dialog->window_ = hwnd;
        }
        else {
            dialog = reinterpret_cast<ScrollableTextDialog*>(::GetWindowLongPtrA(hwnd, GWLP_USERDATA));
        }

        if (dialog) {
            return dialog->HandleMessage(message, wParam, lParam);
        }
        return ::DefWindowProcA(hwnd, message, wParam, lParam);
    }

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message) {
        case WM_CREATE:
            CreateControls();
            return 0;
        case WM_SIZE:
            Layout(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
                done_ = true;
                return 0;
            }
            break;
        case WM_CLOSE:
            done_ = true;
            return 0;
        default:
            break;
        }
        return ::DefWindowProcA(window_, message, wParam, lParam);
    }

    void CreateControls()
    {
        const HINSTANCE instance = ::GetModuleHandleA(nullptr);
        HFONT font = reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));

        edit_ = ::CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "EDIT",
            text_.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL,
            12,
            12,
            720,
            440,
            window_,
            nullptr,
            instance,
            nullptr);
        okButton_ = ::CreateWindowExA(
            0,
            "BUTTON",
            "OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            664,
            466,
            72,
            28,
            window_,
            reinterpret_cast<HMENU>(IDOK),
            instance,
            nullptr);

        ::SendMessageA(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        ::SendMessageA(okButton_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        ::SendMessageA(edit_, EM_SETSEL, 0, 0);
        ::SetFocus(edit_);
    }

    void Layout(int width, int height)
    {
        if (!edit_ || !okButton_) {
            return;
        }

        const int margin = 12;
        const int buttonWidth = 72;
        const int buttonHeight = 28;
        const int buttonY = height - margin - buttonHeight;
        ::MoveWindow(edit_, margin, margin, width - margin * 2, buttonY - margin * 2, TRUE);
        ::MoveWindow(okButton_, width - margin - buttonWidth, buttonY, buttonWidth, buttonHeight, TRUE);
    }

    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    HWND edit_ = nullptr;
    HWND okButton_ = nullptr;
    const char* title_ = "";
    std::string text_;
    bool done_ = false;
};
}

void ShowLauncherHelpDialog(HWND owner)
{
    ShowInfo(
        owner,
        "UtopianModLauncher convention:\r\n\r\n"
        "DLL Mod\r\n"
        "- Contains one or more DLL files under bin\\Final\\mods.\r\n"
        "- The selected DLL is added to LoadOrder.\r\n"
        "- Stage and delay control when the DLL is injected.\r\n"
        "- Mod settings can edit the matching INI next to the DLL.\r\n\r\n"
        "DLL Mod Shared Dependency\r\n"
        "- A common DLL library used by one or more DLL mods.\r\n"
        "- It is still injected through LoadOrder, but marked by [SharedDlls] in GameModLauncher.ini.\r\n"
        "- It owns only its DLL and matching INI manifest; package resources stay with the primary mod.\r\n"
        "- It cannot be deleted while another mod lists it in RequiredBy.\r\n\r\n"
        "DLL Mod Dependency\r\n"
        "- A non-shared DLL installed as part of the same package as a primary DLL mod.\r\n"
        "- Deleting the primary DLL or any DLL dependency deletes the whole package.\r\n"
        "- Shared dependencies are kept when the package is deleted.\r\n\r\n"
        "Resource Mod\r\n"
        "- Contains game resources under data\\..., with no DLL required.\r\n"
        "- Installed files are copied into the game folder.\r\n"
        "- Stage and delay are saved as notes/order metadata only.\r\n"
        "- Resource mods are not injected at launch.\r\n\r\n"
        "Packages should use game-root layout, for example:\r\n"
        "bin\\Final\\mods\\SomeMod.dll\r\n"
        "data\\Scripts\\...\r\n"
        "data\\Textures\\...\r\n\r\n"
        "Install formats\r\n"
        "- UI Install Mod supports folders and .zip/.rar/.7z archives.\r\n"
        "- .zip/.rar/.7z packages can contain DLL mods, resource mods, or both using game-root layout.\r\n"
        "- DLL mods are detected by bin\\Final\\mods\\SomeMod.dll.\r\n"
        "- If a DLL package contains bin\\Final\\GameModLauncher.ini, its [Mods] LoadOrder is used as an install hint.\r\n"
        "- Package [SharedDlls] Names marks helper DLLs as shared dependencies.\r\n"
        "- Optional bin\\Final\\mods\\<ModName>.manifest.ini can provide Description and FilesToDelete.\r\n"
        "- FilesToDelete is backed up/restored or deleted safely during uninstall.\r\n"
        "- If a DLL package contains more DLLs, the launcher shows one dependency table before install.\r\n"
        "- Non-shared dependency DLLs are grouped into one installed package.\r\n"
        "- Existing DLLs can be overwritten or skipped; skip keeps the current DLL file but can update stage/order.\r\n"
        "- Release packages may include data plus bin\\Final\\mods; bundled GameModLauncher files are ignored.\r\n"
        "- Packages without game-root layout can be installed into data or a selected data subfolder.\r\n"
        "- .rar extraction uses Windows shell support or installed WinRAR/UnRAR.\r\n"
        "- .7z extraction uses Windows shell support or installed 7-Zip.\r\n"
        "- Folder install is available from CLI: install --folder <path>.\r\n\r\n"
        "Select a mod in the table to show files installed by that mod.\r\n"
        "Double click a mod to open its settings and rename dialog.\r\n\r\n"
        "Save Changes\r\n"
        "- Saves load order, stages, delay, names and logging without launching the game.\r\n\r\n"
        "When an installed mod overwrites an existing game file, the launcher keeps a backup and restores it on delete only if the file was not changed later. Author cleanup files from FilesToDelete are also backed up before install when they already exist.");
}

void ShowBackedUpFilesDialog(HWND owner, const LauncherConfig& config)
{
    const std::vector<ManifestAuditEntry> audit = GetVanillaFileAudit(config, false, true);
    ShowBackedUpFilesAuditDialog(owner, audit);
}

void ShowBackedUpFilesAuditDialog(HWND owner, const std::vector<ManifestAuditEntry>& audit)
{
    if (audit.empty()) {
        ShowInfo(owner, "No backed-up overwritten or cleanup files were found in installed mod manifests.");
        return;
    }

    std::ostringstream message;
    message << "Backed-up overwritten and cleanup files:\r\n\r\n";
    for (const ManifestAuditEntry& entry : audit) {
        message << entry.modName << "\r\n";
        message << "  " << entry.entry.relativePath << "\r\n";
        message << "  backup: " << entry.entry.backupRelativePath << "\r\n";
        message << "  state: ";
        switch (entry.entry.currentState) {
        case ManifestCurrentState::Missing:
            message << "missing";
            break;
        case ManifestCurrentState::Unchanged:
            message << "unchanged";
            break;
        case ManifestCurrentState::Changed:
            message << "changed";
            break;
        default:
            message << "unknown";
            break;
        }
        message << "\r\n\r\n";
    }

    ScrollableTextDialog(owner, "Backed-up files", message.str()).Show();
}

bool ConfirmPackageConflictsDialog(HWND owner, const std::vector<ModConflictEntry>& conflicts)
{
    if (conflicts.empty()) {
        return true;
    }

    std::ostringstream message;
    message << "This package conflicts with already installed mods:\r\n\r\n";
    const std::size_t maxRows = 20;
    for (std::size_t i = 0; i < conflicts.size() && i < maxRows; ++i) {
        const ModConflictEntry& conflict = conflicts[i];
        message << conflict.relativePath << "\r\n";
        message << "  installed by: " << conflict.otherModName << "\r\n\r\n";
    }

    if (conflicts.size() > maxRows) {
        message << "...and " << static_cast<unsigned long>(conflicts.size() - maxRows) << " more conflict(s).\r\n\r\n";
    }

    message << "Install anyway?";
    return ::MessageBoxA(owner, message.str().c_str(), "Mod file conflicts", MB_YESNO | MB_ICONWARNING) == IDYES;
}
}
