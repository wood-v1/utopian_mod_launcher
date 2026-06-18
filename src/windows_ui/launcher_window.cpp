#include "launcher_window.h"

#include "file_dialogs.h"
#include "install_options_dialog.h"
#include "install_source_dialog.h"
#include "install_source_strategy.h"
#include "launcher_help.h"
#include "mod_settings_dialog.h"
#include "mod_list_view.h"
#include "package_target_dialog.h"
#include "ui_helpers.h"

#include "../launcher_config.h"
#include "../launcher_runtime.h"
#include "../launcher_services.h"
#include "../load_order.h"
#include "../mod_ini.h"
#include "../mod_package.h"
#include "../path_utils.h"
#include "../string_utils.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace uml::windows_ui
{
namespace
{
enum ControlId
{
    IDC_LOGGING_ENABLED = 1001,
    IDC_MOD_LIST,
    IDC_MOVE_UP,
    IDC_MOVE_DOWN,
    IDC_MOD_SETTINGS,
    IDC_INSTALL_MOD,
    IDC_DELETE_MOD,
    IDC_SAVE_CONFIG,
    IDC_LAUNCH_GAME,
    IDC_STAGE_COMBO,
    IDC_DELAY_EDIT,
    IDC_STAGE_HELP,
    IDC_LAUNCHER_HELP,
    IDC_BACKUPS
};

constexpr int kWindowWidth = 760;
constexpr int kWindowHeight = 700;
constexpr UINT kInstalledFilesLoadedMessage = WM_APP + 31;
constexpr UINT kArchivePreparedMessage = WM_APP + 32;
constexpr UINT kPackageInstalledMessage = WM_APP + 33;
constexpr UINT kModDeletedMessage = WM_APP + 34;
constexpr UINT kPackageAnalyzedMessage = WM_APP + 35;
constexpr UINT kBackupsAuditLoadedMessage = WM_APP + 36;
constexpr UINT_PTR kInstalledFilesLoadingTimer = 3001;
constexpr UINT_PTR kBusySpinnerTimer = 3002;

struct PreparedPackagePayload
{
    bool ok = false;
    PreparedInstallPackage package;
    std::string error;
};

struct InstallCompletedPayload
{
    bool ok = false;
    LauncherConfig config;
    InstallModResult result;
    std::string error;
};

struct PackageAnalyzedPayload
{
    bool ok = false;
    std::string packageRoot;
    std::string defaultName;
    std::string packageTargetRelativeDirectory;
    std::vector<PackageFile> files;
    std::string error;
};

struct DeleteCompletedPayload
{
    bool ok = false;
    LauncherConfig config;
    ModDeleteResult result;
    std::string displayName;
    int selectedIndex = -1;
    std::string error;
};

struct BackupsAuditPayload
{
    std::vector<ManifestAuditEntry> audit;
};

}

LauncherWindow::LauncherWindow(HINSTANCE instance)
    : instance_(instance)
{
}

int LauncherWindow::Run()
{
    INITCOMMONCONTROLSEX initControls = {};
    initControls.dwSize = sizeof(initControls);
    initControls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    ::InitCommonControlsEx(&initControls);

    std::string error;
    if (!LoadLauncherConfig(GetLauncherIniPath(), &config_, &error)) {
        ::MessageBoxA(nullptr, error.c_str(), "UtopianModLauncher", MB_ICONWARNING | MB_OK);
        config_ = LauncherConfig();
    }

    WNDCLASSA windowClass = {};
    windowClass.lpfnWndProc = &LauncherWindow::WindowProcSetup;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = "UtopianModLauncherWindow";
    windowClass.hIcon = ::LoadIconA(instance_, MAKEINTRESOURCEA(101));
    windowClass.hCursor = ::LoadCursorA(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);

    ::RegisterClassA(&windowClass);

    window_ = ::CreateWindowExA(
        0,
        windowClass.lpszClassName,
        "UtopianModLauncher",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kWindowWidth,
        kWindowHeight,
        nullptr,
        nullptr,
        instance_,
        this);

    if (!window_) {
        return 1;
    }

    ::ShowWindow(window_, SW_SHOW);
    ::UpdateWindow(window_);

    MSG message = {};
    while (::GetMessageA(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageA(&message);
    }

    if (launchFont_) {
        ::DeleteObject(launchFont_);
    }
    if (titleFont_) {
        ::DeleteObject(titleFont_);
    }
    if (bannerBitmap_) {
        ::DeleteObject(bannerBitmap_);
    }

    return static_cast<int>(message.wParam);
}

LRESULT CALLBACK LauncherWindow::WindowProcSetup(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        CREATESTRUCTA* createStruct = reinterpret_cast<CREATESTRUCTA*>(lParam);
        LauncherWindow* self = reinterpret_cast<LauncherWindow*>(createStruct->lpCreateParams);
        ::SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        ::SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&LauncherWindow::WindowProcThunk));
        self->window_ = window;
        return self->WindowProc(message, wParam, lParam);
    }

    return ::DefWindowProcA(window, message, wParam, lParam);
}

LRESULT CALLBACK LauncherWindow::WindowProcThunk(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    LauncherWindow* self = reinterpret_cast<LauncherWindow*>(::GetWindowLongPtrA(window, GWLP_USERDATA));
    if (!self) {
        return ::DefWindowProcA(window, message, wParam, lParam);
    }

    return self->WindowProc(message, wParam, lParam);
}

LRESULT LauncherWindow::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        CreateControls();
        LoadControlsFromConfig();
        RefreshConflictCache();
        PopulateModList();
        if (!modViews_.empty()) {
            SelectMod(0);
        }
        UpdateActionState();
        return 0;
    case WM_COMMAND:
        HandleCommand(LOWORD(wParam), HIWORD(wParam));
        return 0;
    case WM_NOTIFY:
        return HandleNotify(reinterpret_cast<NMHDR*>(lParam));
    case WM_TIMER:
        if (wParam == kInstalledFilesLoadingTimer) {
            UpdateInstalledFilesLoadingIndicator();
            return 0;
        }
        if (wParam == kBusySpinnerTimer) {
            UpdateBusyIndicator();
            return 0;
        }
        return ::DefWindowProcA(window_, message, wParam, lParam);
    case kInstalledFilesLoadedMessage:
        FinishInstalledFilesLoading(static_cast<int>(wParam), reinterpret_cast<InstalledFilesText*>(lParam));
        return 0;
    case kArchivePreparedMessage:
        FinishArchivePreparation(static_cast<int>(wParam), reinterpret_cast<void*>(lParam));
        return 0;
    case kPackageInstalledMessage:
        FinishPackageInstall(static_cast<int>(wParam), reinterpret_cast<void*>(lParam));
        return 0;
    case kModDeletedMessage:
        FinishModDelete(static_cast<int>(wParam), reinterpret_cast<void*>(lParam));
        return 0;
    case kPackageAnalyzedMessage:
        FinishPackageAnalysis(static_cast<int>(wParam), reinterpret_cast<void*>(lParam));
        return 0;
    case kBackupsAuditLoadedMessage:
        FinishBackupsAudit(static_cast<int>(wParam), reinterpret_cast<void*>(lParam));
        return 0;
    case WM_DESTROY:
        ++installedFilesRequestId_;
        ++busyRequestId_;
        ::KillTimer(window_, kInstalledFilesLoadingTimer);
        ::KillTimer(window_, kBusySpinnerTimer);
        ::PostQuitMessage(0);
        return 0;
    default:
        return ::DefWindowProcA(window_, message, wParam, lParam);
    }
}

HWND LauncherWindow::CreateLabel(const char* text, int x, int y, int width, int height)
{
    return ::CreateWindowExA(0, "STATIC", text, WS_CHILD | WS_VISIBLE, x, y, width, height, window_, nullptr, instance_, nullptr);
}

HWND LauncherWindow::CreateEdit(int id, int x, int y, int width, int height, DWORD extraStyle, DWORD exStyle)
{
    return ::CreateWindowExA(
        exStyle,
        "EDIT",
        "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | extraStyle,
        x,
        y,
        width,
        height,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        instance_,
        nullptr);
}

HWND LauncherWindow::CreateButton(int id, const char* text, int x, int y, int width, int height, DWORD style)
{
    return ::CreateWindowExA(
        0,
        "BUTTON",
        text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
        x,
        y,
        width,
        height,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        instance_,
        nullptr);
}

HWND LauncherWindow::CreateGroup(const char* text, int x, int y, int width, int height)
{
    return ::CreateWindowExA(0, "BUTTON", text, WS_CHILD | WS_VISIBLE | BS_GROUPBOX, x, y, width, height, window_, nullptr, instance_, nullptr);
}

void LauncherWindow::CreateControls()
{
    defaultFont_ = reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
    launchFont_ = ::CreateFontA(18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, "Segoe UI");
    titleFont_ = ::CreateFontA(20, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, "Segoe UI");

    bannerControl_ = ::CreateWindowExA(
        WS_EX_CLIENTEDGE,
        "STATIC",
        "",
        WS_CHILD | WS_VISIBLE | SS_BITMAP | SS_CENTERIMAGE,
        22,
        18,
        500,
        72,
        window_,
        nullptr,
        instance_,
        nullptr);
    titleLabel_ = CreateLabel("Utopian Launcher", 570, 18, 150, 22);
    loggingCheck_ = ::CreateWindowExA(
        0,
        "BUTTON",
        "Logging enabled",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        570,
        42,
        150,
        24,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LOGGING_ENABLED)),
        instance_,
        nullptr);
    helpButton_ = CreateButton(IDC_LAUNCHER_HELP, "Help", 570, 72, 70, 26);
    backupsButton_ = CreateButton(IDC_BACKUPS, "Backups...", 650, 72, 75, 26);

    CreateGroup("Load order", 18, 106, 500, 360);
    modList_ = ::CreateWindowExA(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWA,
        "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        32,
        132,
        472,
        315,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MOD_LIST)),
        instance_,
        nullptr);
    ListView_SetExtendedListViewStyle(modList_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    AddModListColumns(modList_);

    CreateGroup("Selected mod", 535, 106, 190, 215);
    CreateLabel("Load stage", 552, 136, 80, 20);
    stageHelpButton_ = CreateButton(IDC_STAGE_HELP, "?", 684, 132, 20, 22);
    stageCombo_ = ::CreateWindowExA(
        0,
        "COMBOBOX",
        "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
        552,
        158,
        150,
        120,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STAGE_COMBO)),
        instance_,
        nullptr);
    ComboBox_AddString(stageCombo_, "suspended");
    ComboBox_AddString(stageCombo_, "resume");
    ComboBox_AddString(stageCombo_, "engine");
    ComboBox_AddString(stageCombo_, "ui");

    CreateLabel("Delay before load, ms", 552, 194, 140, 20);
    delayEdit_ = CreateEdit(IDC_DELAY_EDIT, 552, 216, 150, 23);

    moveUpButton_ = CreateButton(IDC_MOVE_UP, "Move up", 552, 256, 70, 28);
    moveDownButton_ = CreateButton(IDC_MOVE_DOWN, "Move down", 632, 256, 70, 28);

    settingsButton_ = CreateButton(IDC_MOD_SETTINGS, "Mod settings...", 535, 336, 190, 32);
    installButton_ = CreateButton(IDC_INSTALL_MOD, "Install Mod", 535, 376, 190, 32);
    deleteButton_ = CreateButton(IDC_DELETE_MOD, "Delete Mod", 535, 416, 190, 32);
    saveButton_ = CreateButton(IDC_SAVE_CONFIG, "Save Changes", 535, 456, 190, 32);
    launchButton_ = CreateButton(IDC_LAUNCH_GAME, "Launch", 535, 494, 190, 44, BS_DEFPUSHBUTTON);
    CreateGroup("Installed files", 18, 474, 500, 125);
    installedFilesEdit_ = ::CreateWindowExA(
        WS_EX_CLIENTEDGE,
        "EDIT",
        "Select a mod to show installed files.",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
        32,
        498,
        472,
        78,
        window_,
        nullptr,
        instance_,
        nullptr);
    statusLabel_ = CreateLabel("", 22, 612, 700, 24);

    ApplyDefaultFont(window_);
    ::SendMessageA(titleLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont_), TRUE);
    ::SendMessageA(launchButton_, WM_SETFONT, reinterpret_cast<WPARAM>(launchFont_), TRUE);
    LoadBanner();
}

void LauncherWindow::ApplyDefaultFont(HWND parent)
{
    HWND child = ::GetWindow(parent, GW_CHILD);
    while (child) {
        ::SendMessageA(child, WM_SETFONT, reinterpret_cast<WPARAM>(defaultFont_), TRUE);
        child = ::GetWindow(child, GW_HWNDNEXT);
    }
}

void LauncherWindow::LoadBanner()
{
    const std::string bannerPath = JoinPath(GetModuleDirectory(), "banner.bmp");
    if (!FileExists(bannerPath.c_str())) {
        return;
    }

    bannerBitmap_ = static_cast<HBITMAP>(::LoadImageA(
        nullptr,
        bannerPath.c_str(),
        IMAGE_BITMAP,
        500,
        72,
        LR_LOADFROMFILE));
    if (bannerBitmap_) {
        ::SendMessageA(bannerControl_, STM_SETIMAGE, IMAGE_BITMAP, reinterpret_cast<LPARAM>(bannerBitmap_));
    }
}

void LauncherWindow::LoadControlsFromConfig()
{
    Button_SetCheck(loggingCheck_, config_.loggingEnabled ? BST_CHECKED : BST_UNCHECKED);
}

void LauncherWindow::ReloadConfigAndList(const std::string& selectName)
{
    std::string error;
    if (!LoadLauncherConfig(GetLauncherIniPath(), &config_, &error)) {
        ShowError(window_, error);
        return;
    }

    LoadControlsFromConfig();
    RefreshConflictCache();
    PopulateModList();
    int selected = -1;
    for (std::size_t i = 0; i < modViews_.size(); ++i) {
        if (modViews_[i].type == ModType::Dll
            && _stricmp(config_.mods[modViews_[i].index].dllName.c_str(), selectName.c_str()) == 0) {
            selected = static_cast<int>(i);
            break;
        }
        if (modViews_[i].type == ModType::Resource
            && _stricmp(config_.resourceMods[modViews_[i].index].name.c_str(), selectName.c_str()) == 0) {
            selected = static_cast<int>(i);
            break;
        }
    }

    if (selected >= 0) {
        SelectMod(selected);
    }
    else if (!modViews_.empty()) {
        SelectMod(0);
    }
    else {
        SelectMod(-1);
    }
}

void LauncherWindow::ReadConfigFromControls()
{
    config_.loggingEnabled = Button_GetCheck(loggingCheck_) == BST_CHECKED;
    ApplySelectedModControls(false);
}

void LauncherWindow::RefreshConflictCache()
{
    conflictCache_ = GetInstalledModConflicts(config_);
}

void LauncherWindow::PopulateModList()
{
    PopulateModListView(modList_, config_, &modViews_);
}

void LauncherWindow::SelectMod(int index)
{
    if (index < 0 || index >= static_cast<int>(modViews_.size())) {
        selectedViewIndex_ = -1;
        UpdateActionState();
        return;
    }

    ListView_SetItemState(modList_, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(modList_, index, FALSE);
    selectedViewIndex_ = index;
    LoadSelectedModControls();
    UpdateActionState();
}

int LauncherWindow::GetSelectedModIndex() const
{
    return ListView_GetNextItem(modList_, -1, LVNI_SELECTED);
}

void LauncherWindow::LoadSelectedModControls()
{
    updatingControls_ = true;
    if (selectedViewIndex_ < 0 || selectedViewIndex_ >= static_cast<int>(modViews_.size())) {
        ComboBox_SetCurSel(stageCombo_, 1);
        SetWindowTextString(delayEdit_, "0");
        updatingControls_ = false;
        return;
    }

    const InstalledModView& view = modViews_[static_cast<std::size_t>(selectedViewIndex_)];
    if (view.type == ModType::Dll) {
        const ModEntry& mod = config_.mods[view.index];
        ComboBox_SetCurSel(stageCombo_, StageToComboIndex(mod.stage));
        SetWindowTextString(delayEdit_, Uint32ToString(mod.delayMs));
        const std::string conflictSummary = GetModConflictSummary(config_, conflictCache_, view);
        SetWindowTextString(statusLabel_, conflictSummary.empty() ? "Selected: " + GetDllModDisplayName(mod) : conflictSummary);
    }
    else {
        const ResourceModEntry& mod = config_.resourceMods[view.index];
        ComboBox_SetCurSel(stageCombo_, StageToComboIndex(mod.stage));
        SetWindowTextString(delayEdit_, Uint32ToString(mod.delayMs));
        const std::string conflictSummary = GetModConflictSummary(config_, conflictCache_, view);
        SetWindowTextString(statusLabel_, conflictSummary.empty() ? "Selected: " + GetResourceModDisplayName(mod) + ". Resource stage is note-only." : conflictSummary);
    }
    updatingControls_ = false;
}

void LauncherWindow::ApplySelectedModControls(bool showErrors)
{
    if (updatingControls_ || busy_) {
        return;
    }

    const int index = GetSelectedModIndex();
    if (index < 0 || index >= static_cast<int>(modViews_.size())) {
        return;
    }

    const std::string delayText = Trim(GetWindowTextString(delayEdit_));
    uint32_t delay = 0;
    if (!delayText.empty() && !ParseUint32(delayText, &delay)) {
        if (showErrors) {
            ShowError(window_, "Delay must be a non-negative number.");
        }
        return;
    }

    const InstalledModView& view = modViews_[static_cast<std::size_t>(index)];
    InjectionStage stage = ComboIndexToStage(ComboBox_GetCurSel(stageCombo_));
    if (stage == InjectionStage::Suspended && delay != 0) {
        if (showErrors) {
            ShowError(window_, "Suspended mods cannot have a delay.");
        }
        delay = 0;
        SetWindowTextString(delayEdit_, "0");
    }

    if (view.type == ModType::Dll) {
        ModEntry& mod = config_.mods[view.index];
        mod.stage = stage;
        mod.delayMs = delay;
        mod.spec = SerializeModEntry(mod);
    }
    else {
        ResourceModEntry& mod = config_.resourceMods[view.index];
        mod.stage = stage;
        mod.delayMs = delay;
    }

    PopulateModList();
    SelectMod(index);
}

void LauncherWindow::SetBusy(bool busy)
{
    busy_ = busy;
    UpdateActionState();
}

void LauncherWindow::BeginBusy(const std::string& message)
{
    ++installedFilesRequestId_;
    installedFilesLoading_ = false;
    ::KillTimer(window_, kInstalledFilesLoadingTimer);

    busyMessage_ = message;
    busySpinnerFrame_ = 0;
    busySpinnerActive_ = true;
    SetBusy(true);
    UpdateBusyIndicator();
    ::SetTimer(window_, kBusySpinnerTimer, 120, nullptr);
    ::UpdateWindow(statusLabel_);
}

void LauncherWindow::EndBusy()
{
    busySpinnerActive_ = false;
    ::KillTimer(window_, kBusySpinnerTimer);
    SetBusy(false);
}

void LauncherWindow::UpdateBusyIndicator()
{
    if (!busySpinnerActive_) {
        return;
    }

    static const char kFrames[] = {'|', '/', '-', '\\'};
    const char frame = kFrames[busySpinnerFrame_ % 4];
    ++busySpinnerFrame_;
    std::string status = busyMessage_;
    if (!status.empty() && status.back() != ' ') {
        status += " ";
    }
    status.push_back(frame);
    SetWindowTextString(statusLabel_, status);
}

void LauncherWindow::UpdateActionState()
{
    const bool hasSelection = selectedViewIndex_ >= 0 && selectedViewIndex_ < static_cast<int>(modViews_.size());
    bool canMoveUp = false;
    bool canMoveDown = false;
    bool canOpenSettings = false;
    bool selectedIsDll = false;
    if (hasSelection) {
        const InstalledModView& view = modViews_[static_cast<std::size_t>(selectedViewIndex_)];
        selectedIsDll = view.type == ModType::Dll;
        canMoveUp = view.index > 0;
        canOpenSettings = true;
        if (view.type == ModType::Dll) {
            canMoveDown = view.index + 1 < config_.mods.size();
        }
        else {
            canMoveDown = view.index + 1 < config_.resourceMods.size();
        }
    }
    EnableWindow(loggingCheck_, busy_ ? FALSE : TRUE);
    EnableWindow(helpButton_, busy_ ? FALSE : TRUE);
    EnableWindow(backupsButton_, busy_ ? FALSE : TRUE);
    EnableWindow(modList_, busy_ ? FALSE : TRUE);
    EnableWindow(stageCombo_, !busy_ && hasSelection && selectedIsDll ? TRUE : FALSE);
    EnableWindow(stageHelpButton_, !busy_ && hasSelection && selectedIsDll ? TRUE : FALSE);
    EnableWindow(delayEdit_, !busy_ && hasSelection && selectedIsDll ? TRUE : FALSE);
    EnableWindow(moveUpButton_, !busy_ && canMoveUp ? TRUE : FALSE);
    EnableWindow(moveDownButton_, !busy_ && canMoveDown ? TRUE : FALSE);
    EnableWindow(settingsButton_, !busy_ && canOpenSettings ? TRUE : FALSE);
    EnableWindow(installButton_, busy_ ? FALSE : TRUE);
    EnableWindow(deleteButton_, !busy_ && hasSelection ? TRUE : FALSE);
    EnableWindow(saveButton_, busy_ ? FALSE : TRUE);
    EnableWindow(launchButton_, busy_ ? FALSE : TRUE);
    if (!hasSelection) {
        SetWindowTextString(statusLabel_, "Select a mod to edit its load stage or settings.");
    }
}

void LauncherWindow::MoveSelectedMod(int direction)
{
    ApplySelectedModControls(true);
    const int index = GetSelectedModIndex();
    if (index < 0 || index >= static_cast<int>(modViews_.size())) {
        return;
    }

    const InstalledModView view = modViews_[static_cast<std::size_t>(index)];
    if (view.type == ModType::Dll) {
        const int target = static_cast<int>(view.index) + direction;
        if (target < 0 || target >= static_cast<int>(config_.mods.size())) {
            return;
        }
        std::swap(config_.mods[view.index], config_.mods[static_cast<std::size_t>(target)]);
    }
    else {
        const int target = static_cast<int>(view.index) + direction;
        if (target < 0 || target >= static_cast<int>(config_.resourceMods.size())) {
            return;
        }
        std::swap(config_.resourceMods[view.index], config_.resourceMods[static_cast<std::size_t>(target)]);
    }

    PopulateModList();
    for (std::size_t i = 0; i < modViews_.size(); ++i) {
        if (modViews_[i].type == view.type && modViews_[i].index == static_cast<std::size_t>(static_cast<int>(view.index) + direction)) {
            SelectMod(static_cast<int>(i));
            break;
        }
    }
}

bool LauncherWindow::SaveConfigFromUi()
{
    ReadConfigFromControls();
    std::string error;
    if (!SaveLauncherConfig(GetLauncherIniPath(), config_, &error)) {
        ShowError(window_, error);
        return false;
    }

    SetWindowTextString(statusLabel_, "Saved launcher config.");
    return true;
}

void LauncherWindow::LaunchFromUi()
{
    ReadConfigFromControls();
    std::string error;
    if (!ValidateLauncherConfig(config_, &error)) {
        ShowError(window_, error);
        return;
    }

    if (!SaveConfigFromUi()) {
        return;
    }

    if (!LaunchGame(config_, &error)) {
        ShowError(window_, error);
        return;
    }

    SetWindowTextString(statusLabel_, "Game launched.");
}

void LauncherWindow::OpenSelectedModSettings()
{
    ApplySelectedModControls(true);
    const int index = GetSelectedModIndex();
    if (index < 0 || index >= static_cast<int>(modViews_.size())) {
        return;
    }

    const InstalledModView view = modViews_[static_cast<std::size_t>(index)];
    ModSettingsDialog dialog(instance_, window_, &config_, view);
    const bool configChanged = dialog.ShowModal();
    if (configChanged) {
        std::string error;
        LoadLauncherConfig(GetLauncherIniPath(), &config_, &error);
    }
    PopulateModList();
    SelectMod(index);
}

void LauncherWindow::ShowSelectedModInstalledFiles(bool updateStatus)
{
    const int index = GetSelectedModIndex();
    if (index < 0 || index >= static_cast<int>(modViews_.size())) {
        ++installedFilesRequestId_;
        installedFilesLoading_ = false;
        ::KillTimer(window_, kInstalledFilesLoadingTimer);
        SetWindowTextString(installedFilesEdit_, "Select a mod first.");
        return;
    }

    selectedViewIndex_ = index;
    const InstalledModView view = modViews_[static_cast<std::size_t>(index)];
    StartInstalledFilesLoading(view, updateStatus);
}

void LauncherWindow::StartInstalledFilesLoading(const InstalledModView& view, bool)
{
    const int requestId = ++installedFilesRequestId_;
    installedFilesLoading_ = true;
    installedFilesSpinnerFrame_ = 0;

    SetWindowTextString(installedFilesEdit_, "Loading installed files...\r\nPlease wait.");
    SetWindowTextString(statusLabel_, "Loading installed files |");
    ::SetTimer(window_, kInstalledFilesLoadingTimer, 120, nullptr);
    ::UpdateWindow(installedFilesEdit_);
    ::UpdateWindow(statusLabel_);

    const HWND targetWindow = window_;
    const LauncherConfig configSnapshot = config_;
    std::thread([targetWindow, configSnapshot, view, requestId]() {
        std::unique_ptr<InstalledFilesText> installedFiles(new InstalledFilesText(BuildInstalledFilesText(configSnapshot, view)));
        if (::PostMessageA(targetWindow, kInstalledFilesLoadedMessage, static_cast<WPARAM>(requestId), reinterpret_cast<LPARAM>(installedFiles.get())) != FALSE) {
            installedFiles.release();
        }
    }).detach();
}

void LauncherWindow::FinishInstalledFilesLoading(int requestId, InstalledFilesText* installedFiles)
{
    std::unique_ptr<InstalledFilesText> result(installedFiles);
    if (!result || requestId != installedFilesRequestId_) {
        return;
    }

    installedFilesLoading_ = false;
    ::KillTimer(window_, kInstalledFilesLoadingTimer);
    SetWindowTextString(installedFilesEdit_, result->text);
    SetWindowTextString(statusLabel_, result->status);
}

void LauncherWindow::UpdateInstalledFilesLoadingIndicator()
{
    if (!installedFilesLoading_) {
        return;
    }

    static const char kFrames[] = {'|', '/', '-', '\\'};
    const char frame = kFrames[installedFilesSpinnerFrame_ % 4];
    ++installedFilesSpinnerFrame_;
    std::string status = "Loading installed files ";
    status.push_back(frame);
    SetWindowTextString(statusLabel_, status);
}

void LauncherWindow::InstallMod()
{
    bool useFolder = true;
    InstallSourceDialog sourceDialog(window_);
    if (!sourceDialog.Show(&useFolder)) {
        return;
    }

    std::unique_ptr<InstallSourceStrategy> strategy;
    std::string error;
    if (useFolder) {
        strategy.reset(new FolderInstallStrategy());
    }
    else {
        std::string archivePath;
        if (!PickModPackageFile(window_, &archivePath)) {
            return;
        }

        if (!HasPackageArchiveExtension(archivePath)) {
            ShowError(window_, "Unsupported archive type. Choose a .zip or .rar package.");
            return;
        }
        StartArchivePreparation(archivePath);
        return;
    }

    PreparedInstallPackage package;
    SetWindowTextString(statusLabel_, "Preparing install package...");
    ::UpdateWindow(window_);

    HCURSOR previousCursor = ::SetCursor(::LoadCursorA(nullptr, IDC_WAIT));
    const bool prepared = strategy->Prepare(window_, &package, &error);
    ::SetCursor(previousCursor);
    if (!prepared) {
        if (!error.empty()) {
            ShowError(window_, error);
            SetWindowTextString(statusLabel_, "Install failed while preparing package.");
        }
        return;
    }

    SetWindowTextString(statusLabel_, "Package prepared. Checking contents...");
    ::UpdateWindow(window_);
    InstallModFromPackage(package.packageRoot, package.defaultName);
}

void LauncherWindow::StartArchivePreparation(const std::string& archivePath)
{
    const int requestId = ++busyRequestId_;
    BeginBusy("Preparing install package");

    const HWND targetWindow = window_;
    std::thread([targetWindow, requestId, archivePath]() {
        std::unique_ptr<PreparedPackagePayload> payload(new PreparedPackagePayload());
        ArchiveInstallStrategy strategy(archivePath);
        payload->ok = strategy.Prepare(nullptr, &payload->package, &payload->error);
        if (::PostMessageA(targetWindow, kArchivePreparedMessage, static_cast<WPARAM>(requestId), reinterpret_cast<LPARAM>(payload.get())) != FALSE) {
            payload.release();
        }
    }).detach();
}

void LauncherWindow::FinishArchivePreparation(int requestId, void* payload)
{
    std::unique_ptr<PreparedPackagePayload> result(static_cast<PreparedPackagePayload*>(payload));
    if (!result || requestId != busyRequestId_) {
        return;
    }

    EndBusy();
    if (!result->ok) {
        if (!result->error.empty()) {
            ShowError(window_, result->error);
            SetWindowTextString(statusLabel_, "Install failed while preparing package.");
        }
        return;
    }

    SetWindowTextString(statusLabel_, "Package prepared. Checking contents...");
    ::UpdateWindow(window_);
    InstallModFromPackage(result->package.packageRoot, result->package.defaultName);
}

void LauncherWindow::InstallModFromPackage(const std::string& packageRoot, const std::string& defaultName)
{
    StartPackageAnalysis(packageRoot, defaultName, "");
}

void LauncherWindow::StartPackageAnalysis(const std::string& packageRoot, const std::string& defaultName, const std::string& packageTargetRelativeDirectory)
{
    const int requestId = ++busyRequestId_;
    BeginBusy("Checking package contents");

    const HWND targetWindow = window_;
    std::thread([targetWindow, requestId, packageRoot, defaultName, packageTargetRelativeDirectory]() {
        std::unique_ptr<PackageAnalyzedPayload> payload(new PackageAnalyzedPayload());
        payload->packageRoot = packageRoot;
        payload->defaultName = defaultName;
        payload->packageTargetRelativeDirectory = packageTargetRelativeDirectory;
        if (packageTargetRelativeDirectory.empty()) {
            payload->ok = EnumeratePackageFiles(packageRoot, &payload->files, &payload->error);
        }
        else {
            payload->ok = EnumeratePackageFilesForTarget(packageRoot, packageTargetRelativeDirectory, &payload->files, &payload->error);
        }
        if (::PostMessageA(targetWindow, kPackageAnalyzedMessage, static_cast<WPARAM>(requestId), reinterpret_cast<LPARAM>(payload.get())) != FALSE) {
            payload.release();
        }
    }).detach();
}

void LauncherWindow::FinishPackageAnalysis(int requestId, void* payload)
{
    std::unique_ptr<PackageAnalyzedPayload> result(static_cast<PackageAnalyzedPayload*>(payload));
    if (!result || requestId != busyRequestId_) {
        return;
    }

    EndBusy();
    if (!result->ok) {
        if (result->packageTargetRelativeDirectory.empty()) {
        PackageTargetDialog targetDialog(
            window_,
            "Original validation:\r\n"
                    + (result->error.empty() ? std::string("unsupported layout") : result->error));
            std::string packageTargetRelativeDirectory;
        if (!targetDialog.Show(&packageTargetRelativeDirectory)) {
            SetWindowTextString(statusLabel_, "Install cancelled.");
            return;
        }

            StartPackageAnalysis(result->packageRoot, result->defaultName, packageTargetRelativeDirectory);
        }
        else {
            std::string error = result->error.empty() ? "Package validation failed." : result->error;
            ShowError(window_, error);
            SetWindowTextString(statusLabel_, "Install failed while validating package contents.");
        }
        return;
    }

    ContinueInstallWithPackageFiles(result->packageRoot, result->defaultName, result->packageTargetRelativeDirectory, result->files);
}

void LauncherWindow::ContinueInstallWithPackageFiles(
    const std::string& packageRoot,
    const std::string& defaultName,
    const std::string& packageTargetRelativeDirectory,
    const std::vector<PackageFile>& files)
{
    const std::vector<std::string> dllNames = FindPackageDllNames(files);
    const bool hasResourceFiles = PackageHasResourceFiles(files);
    if (dllNames.empty() && !hasResourceFiles) {
        ShowError(window_, "Package must contain resource files under data or DLL files under bin\\Final\\mods.");
        SetWindowTextString(statusLabel_, "Install failed: package has no supported mod files.");
        return;
    }

    SetWindowTextString(
        statusLabel_,
        dllNames.empty() ? "Resource mod detected. Choose install options." : "DLL mod detected. Choose install options.");
    ::UpdateWindow(window_);

    std::vector<std::string> hintWarnings;
    const std::vector<PackageDllInstallHint> hints = dllNames.empty()
        ? std::vector<PackageDllInstallHint>()
        : GetPackageDllInstallHints(config_, packageRoot, files, &hintWarnings);
    std::vector<std::string> orderedDllNames;
    std::string defaultDllName;
    for (const PackageDllInstallHint& hint : hints) {
        if (hint.presentInPackage) {
            bool alreadyAdded = false;
            for (const std::string& dllName : orderedDllNames) {
                if (_stricmp(dllName.c_str(), hint.dllName.c_str()) == 0) {
                    alreadyAdded = true;
                    break;
                }
            }
            if (!alreadyAdded) {
                orderedDllNames.push_back(hint.dllName);
            }
            if (hint.fromPackageLoadOrder) {
                defaultDllName = hint.dllName;
            }
        }
    }
    for (const std::string& dllName : dllNames) {
        bool alreadyAdded = false;
        for (const std::string& orderedDllName : orderedDllNames) {
            if (_stricmp(orderedDllName.c_str(), dllName.c_str()) == 0) {
                alreadyAdded = true;
                break;
            }
        }
        if (!alreadyAdded) {
            orderedDllNames.push_back(dllName);
        }
    }

    InstallOptions options;
    InstallOptionsDialog dialog(window_, defaultName.empty() ? "New Mod" : defaultName, orderedDllNames.empty() ? dllNames : orderedDllNames, defaultDllName);
    if (!dialog.Show(&options)) {
        SetWindowTextString(statusLabel_, "Install cancelled.");
        return;
    }

    DllInstallDecisions dllDecisions;
    if (!dllNames.empty()) {
        if (!hintWarnings.empty()) {
            std::string warningText = "Package launcher config warnings:\n\n";
            const std::size_t maxWarnings = 8;
            for (std::size_t i = 0; i < hintWarnings.size() && i < maxWarnings; ++i) {
                warningText += "- ";
                warningText += hintWarnings[i];
                warningText += "\n";
            }
            if (hintWarnings.size() > maxWarnings) {
                warningText += "...and more.\n";
            }
            ::MessageBoxA(window_, warningText.c_str(), "Install Mod", MB_ICONWARNING | MB_OK);
        }

        if (!PromptDllInstallDecisions(window_, hints, options.dllName, &dllDecisions)) {
            SetWindowTextString(statusLabel_, "Install cancelled.");
            return;
        }
    }

    const std::vector<ModConflictEntry> packageConflicts = GetPackageConflicts(config_, files);
    if (!ConfirmPackageConflictsDialog(window_, packageConflicts)) {
        SetWindowTextString(statusLabel_, "Install cancelled because package conflicts were not accepted.");
        return;
    }

    InstallModOptions serviceOptions;
    serviceOptions.packageRoot = packageRoot;
    serviceOptions.packageTargetRelativeDirectory = packageTargetRelativeDirectory;
    serviceOptions.name = options.modName;
    serviceOptions.dllName = options.dllName;
    serviceOptions.selectedDllNames = dllDecisions.selectedDllNames;
    serviceOptions.overwriteDllNames = dllDecisions.overwriteDllNames;
    serviceOptions.skipDllNames = dllDecisions.skipDllNames;
    StartPackageInstall(serviceOptions);
}

void LauncherWindow::StartPackageInstall(const InstallModOptions& options)
{
    const int requestId = ++busyRequestId_;
    BeginBusy("Installing mod");

    const HWND targetWindow = window_;
    const LauncherConfig configSnapshot = config_;
    std::thread([targetWindow, requestId, configSnapshot, options]() {
        std::unique_ptr<InstallCompletedPayload> payload(new InstallCompletedPayload());
        payload->config = configSnapshot;
        payload->ok = uml::InstallModFromPackage(&payload->config, options, &payload->result, &payload->error);
        if (::PostMessageA(targetWindow, kPackageInstalledMessage, static_cast<WPARAM>(requestId), reinterpret_cast<LPARAM>(payload.get())) != FALSE) {
            payload.release();
        }
    }).detach();
}

void LauncherWindow::FinishPackageInstall(int requestId, void* payload)
{
    std::unique_ptr<InstallCompletedPayload> resultPayload(static_cast<InstallCompletedPayload*>(payload));
    if (!resultPayload || requestId != busyRequestId_) {
        return;
    }

    EndBusy();
    if (!resultPayload->ok) {
        ShowError(window_, resultPayload->error);
        SetWindowTextString(statusLabel_, "Install failed.");
        return;
    }

    config_ = resultPayload->config;
    std::string error;
    if (!SaveLauncherConfig(GetLauncherIniPath(), config_, &error)) {
        ShowError(window_, error);
        return;
    }

    RefreshConflictCache();
    PopulateModList();
    const InstallModResult& result = resultPayload->result;
    for (std::size_t i = 0; i < modViews_.size(); ++i) {
        const InstalledModView& view = modViews_[i];
        if (result.type == ModType::Dll
            && view.type == ModType::Dll
            && _stricmp(config_.mods[view.index].dllName.c_str(), result.manifestOwner.c_str()) == 0) {
            SelectMod(static_cast<int>(i));
            break;
        }
        if (result.type == ModType::Resource
            && view.type == ModType::Resource
            && _stricmp(config_.resourceMods[view.index].id.c_str(), result.manifestOwner.c_str()) == 0) {
            SelectMod(static_cast<int>(i));
            break;
        }
    }

    SetWindowTextString(statusLabel_, "Installed " + result.displayName + ".");
}

void LauncherWindow::DeleteSelectedMod()
{
    const int index = GetSelectedModIndex();
    if (index < 0 || index >= static_cast<int>(modViews_.size())) {
        return;
    }

    const InstalledModView view = modViews_[static_cast<std::size_t>(index)];
    const ModMatch match{view.type, view.index};
    const std::string displayName = GetModDisplayName(config_, match);
    const int confirmation = ::MessageBoxA(
        window_,
        ("Delete " + displayName + " from disk?\n\n"
         "This will remove the mod from the launcher and delete the files it installed.\n\n"
         "If the mod replaced original game files, the launcher will restore its saved backups.\n"
         "If a file was edited after the mod was installed, it will be kept as-is.\n\n"
         "Continue?").c_str(),
        "Delete Mod",
        MB_YESNO | MB_ICONWARNING);
    if (confirmation != IDYES) {
        return;
    }

    StartModDelete(match, displayName, index);
}

void LauncherWindow::StartModDelete(const ModMatch& match, const std::string& displayName, int selectedIndex)
{
    const int requestId = ++busyRequestId_;
    BeginBusy("Deleting mod");

    const HWND targetWindow = window_;
    const LauncherConfig configSnapshot = config_;
    std::thread([targetWindow, requestId, configSnapshot, match, displayName, selectedIndex]() {
        std::unique_ptr<DeleteCompletedPayload> payload(new DeleteCompletedPayload());
        payload->config = configSnapshot;
        payload->displayName = displayName;
        payload->selectedIndex = selectedIndex;
        payload->ok = uml::DeleteInstalledMod(&payload->config, match, &payload->result, &payload->error);
        if (::PostMessageA(targetWindow, kModDeletedMessage, static_cast<WPARAM>(requestId), reinterpret_cast<LPARAM>(payload.get())) != FALSE) {
            payload.release();
        }
    }).detach();
}

void LauncherWindow::FinishModDelete(int requestId, void* payload)
{
    std::unique_ptr<DeleteCompletedPayload> resultPayload(static_cast<DeleteCompletedPayload*>(payload));
    if (!resultPayload || requestId != busyRequestId_) {
        return;
    }

    EndBusy();
    if (!resultPayload->ok) {
        ShowError(window_, resultPayload->error);
        SetWindowTextString(statusLabel_, "Delete failed.");
        return;
    }

    config_ = resultPayload->config;
    std::string error;
    if (!SaveLauncherConfig(GetLauncherIniPath(), config_, &error)) {
        ShowError(window_, error);
        return;
    }

    RefreshConflictCache();
    PopulateModList();
    if (!modViews_.empty()) {
        SelectMod(std::min(resultPayload->selectedIndex, static_cast<int>(modViews_.size() - 1)));
    }
    else {
        SelectMod(-1);
    }

    SetWindowTextString(
        statusLabel_,
        "Deleted " + resultPayload->displayName + ": "
            + Uint32ToString(static_cast<uint32_t>(resultPayload->result.deletedRelativePaths.size())) + " removed, "
            + Uint32ToString(static_cast<uint32_t>(resultPayload->result.restoredRelativePaths.size())) + " restored, "
            + Uint32ToString(static_cast<uint32_t>(resultPayload->result.skippedRelativePaths.size())) + " skipped.");
    if (!resultPayload->result.skippedRelativePaths.empty()) {
        ShowInfo(window_, "Some files were changed after install and were left untouched.");
    }
}

void LauncherWindow::StartBackupsAudit()
{
    const int requestId = ++busyRequestId_;
    BeginBusy("Loading backups");

    const HWND targetWindow = window_;
    const LauncherConfig configSnapshot = config_;
    std::thread([targetWindow, requestId, configSnapshot]() {
        std::unique_ptr<BackupsAuditPayload> payload(new BackupsAuditPayload());
        payload->audit = GetVanillaFileAudit(configSnapshot, false, true);
        if (::PostMessageA(targetWindow, kBackupsAuditLoadedMessage, static_cast<WPARAM>(requestId), reinterpret_cast<LPARAM>(payload.get())) != FALSE) {
            payload.release();
        }
    }).detach();
}

void LauncherWindow::FinishBackupsAudit(int requestId, void* payload)
{
    std::unique_ptr<BackupsAuditPayload> result(static_cast<BackupsAuditPayload*>(payload));
    if (!result || requestId != busyRequestId_) {
        return;
    }

    EndBusy();
    SetWindowTextString(statusLabel_, "Backups loaded.");
    ShowBackedUpFilesAuditDialog(window_, result->audit);
}

void LauncherWindow::HandleCommand(int id, int notification)
{
    switch (id) {
    case IDC_LOGGING_ENABLED:
        if (notification == BN_CLICKED) {
            ReadConfigFromControls();
        }
        break;
    case IDC_STAGE_COMBO:
        if (notification == CBN_SELCHANGE) {
            ApplySelectedModControls(true);
        }
        break;
    case IDC_DELAY_EDIT:
        if (notification == EN_KILLFOCUS) {
            ApplySelectedModControls(true);
        }
        break;
    case IDC_MOVE_UP:
        MoveSelectedMod(-1);
        break;
    case IDC_MOVE_DOWN:
        MoveSelectedMod(1);
        break;
    case IDC_MOD_SETTINGS:
        OpenSelectedModSettings();
        break;
    case IDC_INSTALL_MOD:
        InstallMod();
        break;
    case IDC_DELETE_MOD:
        DeleteSelectedMod();
        break;
    case IDC_STAGE_HELP:
        ShowInfo(
            window_,
            "Load stage controls when a DLL mod is injected:\r\n\r\n"
            "suspended - inject before the game main thread starts.\r\n"
            "resume - start the game, then inject immediately.\r\n"
            "engine - wait until Engine.dll is loaded.\r\n"
            "ui - wait until Engine.dll, then UI.dll are loaded.\r\n\r\n"
            "For Resource Mod entries this is note-only; resource files are already installed on disk.");
        break;
    case IDC_LAUNCHER_HELP:
        ShowLauncherHelpDialog(window_);
        break;
    case IDC_BACKUPS:
        StartBackupsAudit();
        break;
    case IDC_SAVE_CONFIG:
        SaveConfigFromUi();
        break;
    case IDC_LAUNCH_GAME:
        LaunchFromUi();
        break;
    default:
        break;
    }
}

LRESULT LauncherWindow::HandleNotify(NMHDR* header)
{
    if (!header) {
        return 0;
    }

    if (header->idFrom != IDC_MOD_LIST) {
        return 0;
    }

    if (header->code == NM_CUSTOMDRAW) {
        NMLVCUSTOMDRAW* customDraw = reinterpret_cast<NMLVCUSTOMDRAW*>(header);
        if (customDraw->nmcd.dwDrawStage == CDDS_PREPAINT) {
            return CDRF_NOTIFYITEMDRAW;
        }
        if (customDraw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
            const int row = static_cast<int>(customDraw->nmcd.dwItemSpec);
            if (row >= 0 && row < static_cast<int>(modViews_.size()) && IsModViewConflicting(config_, conflictCache_, modViews_[static_cast<std::size_t>(row)])) {
                customDraw->clrTextBk = RGB(255, 226, 236);
            }
            return CDRF_DODEFAULT;
        }
    }

    if (header->code == NM_DBLCLK) {
        const int index = GetSelectedModIndex();
        if (index >= 0) {
            selectedViewIndex_ = index;
            OpenSelectedModSettings();
        }
        return 0;
    }

    if (header->code == LVN_ITEMCHANGED) {
        NMLISTVIEW* listView = reinterpret_cast<NMLISTVIEW*>(header);
        if ((listView->uChanged & LVIF_STATE) == 0 || (listView->uNewState & LVIS_SELECTED) == 0) {
            return 0;
        }

        selectedViewIndex_ = listView->iItem;
        LoadSelectedModControls();
        ShowSelectedModInstalledFiles(false);
        UpdateActionState();
    }
    return 0;
}

}
