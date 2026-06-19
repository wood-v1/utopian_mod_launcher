#include "install_options_dialog.h"

#include "../load_order.h"
#include "../mod_package.h"
#include "../string_utils.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <utility>

namespace uml::windows_ui
{
namespace
{
enum DllDecisionControlId
{
    IDC_DLL_DECISION_LIST = 4101,
    IDC_DLL_OVERWRITE_SELECTED,
    IDC_DLL_SKIP_SELECTED
};

constexpr UINT kApplyInitialDllChecksMessage = WM_APP + 51;

class DllInstallDecisionDialog
{
public:
    DllInstallDecisionDialog(HWND owner, std::vector<PackageDllInstallHint> hints, std::string primaryDllName)
        : owner_(owner)
        , hints_(std::move(hints))
        , primaryDllName_(std::move(primaryDllName))
        , defaultSelected_(hints_.size(), false)
        , selected_(hints_.size(), false)
        , overwrite_(hints_.size(), false)
    {
        for (std::size_t i = 0; i < hints_.size(); ++i) {
            defaultSelected_[i] = hints_[i].presentInPackage
                || hints_[i].fromPackageLoadOrder
                || _stricmp(hints_[i].dllName.c_str(), primaryDllName_.c_str()) == 0;
            selected_[i] = defaultSelected_[i];
            overwrite_[i] = false;
        }
    }

    bool Show(DllInstallDecisions* decisions)
    {
        WNDCLASSA windowClass = {};
        windowClass.lpfnWndProc = &DllInstallDecisionDialog::WindowProcSetup;
        windowClass.hInstance = ::GetModuleHandleA(nullptr);
        windowClass.lpszClassName = "UtopianDllInstallDecisionDialog";
        windowClass.hCursor = ::LoadCursorA(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        ::RegisterClassA(&windowClass);

        RECT parentRect = {};
        ::GetWindowRect(owner_, &parentRect);
        const int width = 760;
        const int height = 430;
        const int x = parentRect.left + ((parentRect.right - parentRect.left) - width) / 2;
        const int y = parentRect.top + ((parentRect.bottom - parentRect.top) - height) / 2;

        window_ = ::CreateWindowExA(
            WS_EX_DLGMODALFRAME,
            windowClass.lpszClassName,
            "DLL Dependencies",
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
            ::MessageBoxA(owner_, "Failed to open DLL dependency dialog.", "Install Mod", MB_ICONERROR | MB_OK);
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
        if (accepted_ && decisions) {
            *decisions = decisions_;
        }
        return accepted_;
    }

private:
    static LRESULT CALLBACK WindowProcSetup(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE) {
            CREATESTRUCTA* createStruct = reinterpret_cast<CREATESTRUCTA*>(lParam);
            DllInstallDecisionDialog* self = reinterpret_cast<DllInstallDecisionDialog*>(createStruct->lpCreateParams);
            ::SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            ::SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&DllInstallDecisionDialog::WindowProcThunk));
            self->window_ = window;
            return self->WindowProc(message, wParam, lParam);
        }

        return ::DefWindowProcA(window, message, wParam, lParam);
    }

    static LRESULT CALLBACK WindowProcThunk(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        DllInstallDecisionDialog* self = reinterpret_cast<DllInstallDecisionDialog*>(::GetWindowLongPtrA(window, GWLP_USERDATA));
        if (!self) {
            return ::DefWindowProcA(window, message, wParam, lParam);
        }

        return self->WindowProc(message, wParam, lParam);
    }

    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
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
            if (LOWORD(wParam) == IDC_DLL_OVERWRITE_SELECTED) {
                SetSelectedFileAction(true);
                return 0;
            }
            if (LOWORD(wParam) == IDC_DLL_SKIP_SELECTED) {
                SetSelectedFileAction(false);
                return 0;
            }
            break;
        case kApplyInitialDllChecksMessage:
            ApplyInitialChecks();
            return 0;
        case WM_NOTIFY:
            if (reinterpret_cast<NMHDR*>(lParam)->hwndFrom == list_) {
                HandleListNotify(reinterpret_cast<NMHDR*>(lParam));
            }
            return 0;
        case WM_CLOSE:
            ::DestroyWindow(window_);
            return 0;
        default:
            break;
        }
        return ::DefWindowProcA(window_, message, wParam, lParam);
    }

    void AddColumn(int index, const char* text, int width)
    {
        LVCOLUMNA column = {};
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = const_cast<char*>(text);
        column.cx = width;
        column.iSubItem = index;
        ListView_InsertColumn(list_, index, &column);
    }

    void SetSubItemText(int item, int subItem, const std::string& text)
    {
        ListView_SetItemText(list_, item, subItem, const_cast<char*>(text.c_str()));
    }

    void CreateControls()
    {
        HFONT font = reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
        ::CreateWindowExA(
            0,
            "STATIC",
            "Choose DLLs to add to LoadOrder. Existing DLL files can be overwritten or kept.",
            WS_CHILD | WS_VISIBLE,
            16,
            14,
            700,
            20,
            window_,
            nullptr,
            ::GetModuleHandleA(nullptr),
            nullptr);

        list_ = ::CreateWindowExA(
            WS_EX_CLIENTEDGE,
            WC_LISTVIEWA,
            "",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            16,
            42,
            720,
            235,
            window_,
            reinterpret_cast<HMENU>(IDC_DLL_DECISION_LIST),
            ::GetModuleHandleA(nullptr),
            nullptr);
        ListView_SetExtendedListViewStyle(list_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_CHECKBOXES);
        AddColumn(0, "DLL", 150);
        AddColumn(1, "Package order", 90);
        AddColumn(2, "Stage", 72);
        AddColumn(3, "Delay", 60);
        AddColumn(4, "Installed", 74);
        AddColumn(5, "File action", 110);

        int packageOrder = 1;
        for (std::size_t i = 0; i < hints_.size(); ++i) {
            LVITEMA item = {};
            item.mask = LVIF_TEXT | LVIF_STATE;
            item.iItem = static_cast<int>(i);
            const std::string dllText = hints_[i].sharedDependency ? hints_[i].dllName + " [shared]" : hints_[i].dllName;
            item.pszText = const_cast<char*>(dllText.c_str());
            item.stateMask = LVIS_STATEIMAGEMASK;
            item.state = INDEXTOSTATEIMAGEMASK(selected_[i] ? 2 : 1);
            ListView_InsertItem(list_, &item);
            SetSubItemText(static_cast<int>(i), 1, hints_[i].fromPackageLoadOrder ? std::to_string(packageOrder++) : "");
            SetSubItemText(static_cast<int>(i), 2, GetStageName(hints_[i].stage));
            SetSubItemText(static_cast<int>(i), 3, Uint32ToString(hints_[i].delayMs));
            SetSubItemText(static_cast<int>(i), 4, (hints_[i].installedInConfig || hints_[i].targetFileExists) ? "Yes" : "No");
            UpdateActionText(static_cast<int>(i));
        }

        ::CreateWindowExA(
            0,
            "STATIC",
            "Skip keeps the existing DLL file; stage/order may still be updated in launcher config.",
            WS_CHILD | WS_VISIBLE,
            16,
            288,
            700,
            32,
            window_,
            nullptr,
            ::GetModuleHandleA(nullptr),
            nullptr);

        HWND overwrite = ::CreateWindowExA(0, "BUTTON", "Overwrite selected", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 16, 326, 130, 28, window_, reinterpret_cast<HMENU>(IDC_DLL_OVERWRITE_SELECTED), ::GetModuleHandleA(nullptr), nullptr);
        HWND skip = ::CreateWindowExA(0, "BUTTON", "Skip selected", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 156, 326, 100, 28, window_, reinterpret_cast<HMENU>(IDC_DLL_SKIP_SELECTED), ::GetModuleHandleA(nullptr), nullptr);
        HWND ok = ::CreateWindowExA(0, "BUTTON", "Install", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 556, 326, 80, 28, window_, reinterpret_cast<HMENU>(IDOK), ::GetModuleHandleA(nullptr), nullptr);
        HWND cancel = ::CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 646, 326, 80, 28, window_, reinterpret_cast<HMENU>(IDCANCEL), ::GetModuleHandleA(nullptr), nullptr);

        HWND child = ::GetWindow(window_, GW_CHILD);
        while (child) {
            ::SendMessageA(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            child = ::GetWindow(child, GW_HWNDNEXT);
        }
        ::SendMessageA(overwrite, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        ::SendMessageA(skip, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        ::SendMessageA(ok, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        ::SendMessageA(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        ::PostMessageA(window_, kApplyInitialDllChecksMessage, 0, 0);
    }

    void HandleListNotify(NMHDR* header)
    {
        if (header->code != LVN_ITEMCHANGED) {
            return;
        }

        NMLISTVIEW* change = reinterpret_cast<NMLISTVIEW*>(header);
        if (change->iItem < 0 || change->iItem >= static_cast<int>(selected_.size())) {
            return;
        }
        if (!initialChecksApplied_ || applyingInitialChecks_) {
            return;
        }
        selected_[static_cast<std::size_t>(change->iItem)] = ListView_GetCheckState(list_, change->iItem) != FALSE;
    }

    void ApplyInitialChecks()
    {
        if (!list_) {
            return;
        }

        applyingInitialChecks_ = true;
        for (std::size_t i = 0; i < defaultSelected_.size(); ++i) {
            selected_[i] = defaultSelected_[i];
            ListView_SetCheckState(list_, static_cast<int>(i), defaultSelected_[i] ? TRUE : FALSE);
        }
        applyingInitialChecks_ = false;
        initialChecksApplied_ = true;
    }

    void UpdateActionText(int index)
    {
        const PackageDllInstallHint& hint = hints_[static_cast<std::size_t>(index)];
        std::string text = "New";
        if (!hint.presentInPackage) {
            text = "Existing";
        }
        else if (hint.targetFileExists) {
            text = overwrite_[static_cast<std::size_t>(index)] ? "Overwrite" : (hint.sharedDependency ? "Keep existing" : "Skip");
        }
        SetSubItemText(index, 5, text);
    }

    void SetSelectedFileAction(bool overwrite)
    {
        const int index = ListView_GetNextItem(list_, -1, LVNI_SELECTED);
        if (index < 0 || index >= static_cast<int>(hints_.size())) {
            return;
        }
        if (!hints_[static_cast<std::size_t>(index)].presentInPackage || !hints_[static_cast<std::size_t>(index)].targetFileExists) {
            return;
        }
        overwrite_[static_cast<std::size_t>(index)] = overwrite;
        UpdateActionText(index);
    }

    void Accept()
    {
        decisions_ = DllInstallDecisions();
        bool primarySelected = false;
        for (std::size_t i = 0; i < hints_.size(); ++i) {
            selected_[i] = ListView_GetCheckState(list_, static_cast<int>(i)) != FALSE;
            if (!selected_[i]) {
                continue;
            }

            decisions_.selectedDllNames.push_back(hints_[i].dllName);
            if (_stricmp(hints_[i].dllName.c_str(), primaryDllName_.c_str()) == 0) {
                primarySelected = true;
            }
            if (hints_[i].presentInPackage && hints_[i].targetFileExists) {
                if (overwrite_[i]) {
                    decisions_.overwriteDllNames.push_back(hints_[i].dllName);
                }
                else {
                    decisions_.skipDllNames.push_back(hints_[i].dllName);
                }
            }
        }

        if (!primarySelected) {
            ::MessageBoxA(window_, "Primary DLL must be selected.", "Install Mod", MB_ICONWARNING | MB_OK);
            return;
        }

        accepted_ = true;
        ::DestroyWindow(window_);
    }

    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    HWND list_ = nullptr;
    std::vector<PackageDllInstallHint> hints_;
    std::string primaryDllName_;
    std::vector<bool> defaultSelected_;
    std::vector<bool> selected_;
    std::vector<bool> overwrite_;
    bool applyingInitialChecks_ = false;
    bool initialChecksApplied_ = false;
    bool accepted_ = false;
    DllInstallDecisions decisions_;
};
}

InstallOptionsDialog::InstallOptionsDialog(HWND owner, const std::string& defaultName, const std::vector<std::string>& dllNames, const std::string& defaultDllName)
    : owner_(owner)
    , defaultName_(defaultName)
    , defaultDllName_(defaultDllName)
    , dllNames_(dllNames)
{
}

bool InstallOptionsDialog::Show(InstallOptions* options)
{
    WNDCLASSA windowClass = {};
    windowClass.lpfnWndProc = &InstallOptionsDialog::WindowProcSetup;
    windowClass.hInstance = ::GetModuleHandleA(nullptr);
    windowClass.lpszClassName = "UtopianInstallOptionsDialog";
    windowClass.hCursor = ::LoadCursorA(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    ::RegisterClassA(&windowClass);

    RECT parentRect = {};
    ::GetWindowRect(owner_, &parentRect);
    const int width = 390;
    const int height = dllNames_.empty() ? 220 : 270;
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
        ::MessageBoxA(
            owner_,
            ("Failed to open install options dialog. Win32 error: " + std::to_string(::GetLastError())).c_str(),
            "Install Mod",
            MB_ICONERROR | MB_OK);
        return false;
    }

    ::EnableWindow(owner_, FALSE);
    ::ShowWindow(window_, SW_SHOW);
    ::SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    ::SetWindowPos(window_, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
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
    if (accepted_ && options) {
        *options = options_;
    }
    return accepted_;
}

LRESULT CALLBACK InstallOptionsDialog::WindowProcSetup(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        CREATESTRUCTA* createStruct = reinterpret_cast<CREATESTRUCTA*>(lParam);
        InstallOptionsDialog* self = reinterpret_cast<InstallOptionsDialog*>(createStruct->lpCreateParams);
        ::SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        ::SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&InstallOptionsDialog::WindowProcThunk));
        self->window_ = window;
        return self->WindowProc(message, wParam, lParam);
    }

    return ::DefWindowProcA(window, message, wParam, lParam);
}

LRESULT CALLBACK InstallOptionsDialog::WindowProcThunk(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    InstallOptionsDialog* self = reinterpret_cast<InstallOptionsDialog*>(::GetWindowLongPtrA(window, GWLP_USERDATA));
    if (!self) {
        return ::DefWindowProcA(window, message, wParam, lParam);
    }

    return self->WindowProc(message, wParam, lParam);
}

LRESULT InstallOptionsDialog::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
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

void InstallOptionsDialog::CreateControls()
{
    HFONT font = reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
    const bool isDllMod = !dllNames_.empty();
    ::CreateWindowExA(0, "STATIC", isDllMod ? "Type: DLL Mod" : "Type: Resource Mod", WS_CHILD | WS_VISIBLE, 16, 16, 320, 20, window_, nullptr, ::GetModuleHandleA(nullptr), nullptr);
    ::CreateWindowExA(0, "STATIC", "Mod name", WS_CHILD | WS_VISIBLE, 16, 48, 100, 20, window_, nullptr, ::GetModuleHandleA(nullptr), nullptr);
    nameEdit_ = ::CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", defaultName_.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 116, 46, 240, 23, window_, nullptr, ::GetModuleHandleA(nullptr), nullptr);

    int buttonY = 130;
    if (isDllMod) {
        ::CreateWindowExA(0, "STATIC", "DLL for LoadOrder", WS_CHILD | WS_VISIBLE, 16, 84, 120, 20, window_, nullptr, ::GetModuleHandleA(nullptr), nullptr);
        dllCombo_ = ::CreateWindowExA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 136, 82, 220, 120, window_, nullptr, ::GetModuleHandleA(nullptr), nullptr);
        for (const std::string& dllName : dllNames_) {
            ComboBox_AddString(dllCombo_, dllName.c_str());
        }
        int selectedIndex = 0;
        if (!defaultDllName_.empty()) {
            for (std::size_t i = 0; i < dllNames_.size(); ++i) {
                if (_stricmp(dllNames_[i].c_str(), defaultDllName_.c_str()) == 0) {
                    selectedIndex = static_cast<int>(i);
                    break;
                }
            }
        }
        ComboBox_SetCurSel(dllCombo_, selectedIndex);
        buttonY = 178;
    }
    else {
        ::CreateWindowExA(0, "STATIC", "No DLL found. This package will be installed as game resources.", WS_CHILD | WS_VISIBLE, 16, 84, 340, 34, window_, nullptr, ::GetModuleHandleA(nullptr), nullptr);
    }

    HWND ok = ::CreateWindowExA(0, "BUTTON", "Install", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 186, buttonY, 80, 28, window_, reinterpret_cast<HMENU>(IDOK), ::GetModuleHandleA(nullptr), nullptr);
    HWND cancel = ::CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 276, buttonY, 80, 28, window_, reinterpret_cast<HMENU>(IDCANCEL), ::GetModuleHandleA(nullptr), nullptr);

    HWND child = ::GetWindow(window_, GW_CHILD);
    while (child) {
        ::SendMessageA(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        child = ::GetWindow(child, GW_HWNDNEXT);
    }
    ::SendMessageA(ok, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    ::SendMessageA(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void InstallOptionsDialog::Accept()
{
    char nameBuffer[260] = {};
    ::GetWindowTextA(nameEdit_, nameBuffer, sizeof(nameBuffer));
    options_.modName = Trim(nameBuffer);
    if (options_.modName.empty()) {
        ::MessageBoxA(window_, "Mod name cannot be empty.", "Install Mod", MB_ICONWARNING | MB_OK);
        return;
    }

    options_.isDllMod = !dllNames_.empty();
    if (options_.isDllMod) {
        const int index = ComboBox_GetCurSel(dllCombo_);
        if (index == CB_ERR) {
            return;
        }
        char dllBuffer[MAX_PATH] = {};
        ComboBox_GetLBText(dllCombo_, index, dllBuffer);
        options_.dllName = dllBuffer;
        options_.manifestOwner = options_.dllName;
    }
    else {
        options_.manifestOwner = SanitizeManifestOwner(options_.modName);
    }

    accepted_ = true;
    ::DestroyWindow(window_);
}

bool PromptDllInstallDecisions(
    HWND owner,
    const std::vector<PackageDllInstallHint>& hints,
    const std::string& primaryDllName,
    DllInstallDecisions* decisions)
{
    if (decisions) {
        *decisions = DllInstallDecisions();
    }

    DllInstallDecisionDialog dialog(owner, hints, primaryDllName);
    return dialog.Show(decisions);
}
}
