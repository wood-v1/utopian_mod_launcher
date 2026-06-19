#include "mod_settings_dialog.h"

#include "ui_helpers.h"

#include "../launcher_config.h"
#include "../launcher_services.h"
#include "../mod_ini.h"
#include "../path_utils.h"
#include "../string_utils.h"

#include <commctrl.h>

namespace uml::windows_ui
{
namespace
{
enum ControlId
{
    IDC_SETTINGS_LIST = 3001,
    IDC_NAME_EDIT,
    IDC_SECTION_EDIT,
    IDC_KEY_EDIT,
    IDC_VALUE_EDIT,
    IDC_RAW_EDIT,
    IDC_APPLY_SETTING,
    IDC_ADD_SETTING,
    IDC_REMOVE_SETTING,
    IDC_CREATE_INI,
    IDC_SAVE_INI,
    IDC_CLOSE_DIALOG
};

constexpr int kDialogWidth = 720;
constexpr int kDialogHeight = 470;
}

ModSettingsDialog::ModSettingsDialog(HINSTANCE instance, HWND parent, LauncherConfig* config, const InstalledModView& modView)
    : instance_(instance)
    , parent_(parent)
    , config_(config)
    , modView_(modView)
{
}

bool ModSettingsDialog::ShowModal()
{
    WNDCLASSA windowClass = {};
    windowClass.lpfnWndProc = &ModSettingsDialog::WindowProcSetup;
    windowClass.hInstance = instance_;
    windowClass.lpszClassName = "UtopianModSettingsDialog";
    windowClass.hIcon = ::LoadIconA(instance_, MAKEINTRESOURCEA(101));
    windowClass.hCursor = ::LoadCursorA(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    ::RegisterClassA(&windowClass);

    RECT parentRect = {};
    ::GetWindowRect(parent_, &parentRect);
    const int x = parentRect.left + ((parentRect.right - parentRect.left) - kDialogWidth) / 2;
    const int y = parentRect.top + ((parentRect.bottom - parentRect.top) - kDialogHeight) / 2;

    window_ = ::CreateWindowExA(
        WS_EX_DLGMODALFRAME,
        windowClass.lpszClassName,
        "Mod settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x,
        y,
        kDialogWidth,
        kDialogHeight,
        parent_,
        nullptr,
        instance_,
        this);

    if (!window_) {
        return false;
    }

    ::EnableWindow(parent_, FALSE);
    ::ShowWindow(window_, SW_SHOW);
    ::UpdateWindow(window_);

    MSG message = {};
    while (::IsWindow(window_) && ::GetMessageA(&message, nullptr, 0, 0) > 0) {
        if (!::IsDialogMessageA(window_, &message)) {
            ::TranslateMessage(&message);
            ::DispatchMessageA(&message);
        }
    }

    ::EnableWindow(parent_, TRUE);
    ::SetActiveWindow(parent_);
    return configChanged_;
}

LRESULT CALLBACK ModSettingsDialog::WindowProcSetup(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        CREATESTRUCTA* createStruct = reinterpret_cast<CREATESTRUCTA*>(lParam);
        ModSettingsDialog* self = reinterpret_cast<ModSettingsDialog*>(createStruct->lpCreateParams);
        ::SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        ::SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&ModSettingsDialog::WindowProcThunk));
        self->window_ = window;
        return self->WindowProc(message, wParam, lParam);
    }

    return ::DefWindowProcA(window, message, wParam, lParam);
}

LRESULT CALLBACK ModSettingsDialog::WindowProcThunk(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    ModSettingsDialog* self = reinterpret_cast<ModSettingsDialog*>(::GetWindowLongPtrA(window, GWLP_USERDATA));
    if (!self) {
        return ::DefWindowProcA(window, message, wParam, lParam);
    }

    return self->WindowProc(message, wParam, lParam);
}

LRESULT ModSettingsDialog::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        CreateControls();
        LoadName();
        LoadDocument();
        return 0;
    case WM_COMMAND:
        HandleCommand(LOWORD(wParam));
        return 0;
    case WM_NOTIFY:
        HandleNotify(reinterpret_cast<NMHDR*>(lParam));
        return 0;
    case WM_CLOSE:
        Close();
        return 0;
    default:
        return ::DefWindowProcA(window_, message, wParam, lParam);
    }
}

HWND ModSettingsDialog::CreateLabel(const char* text, int x, int y, int width, int height)
{
    return ::CreateWindowExA(0, "STATIC", text, WS_CHILD | WS_VISIBLE, x, y, width, height, window_, nullptr, instance_, nullptr);
}

HWND ModSettingsDialog::CreateEdit(int id, int x, int y, int width, int height, DWORD extraStyle)
{
    return ::CreateWindowExA(
        WS_EX_CLIENTEDGE,
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

HWND ModSettingsDialog::CreateButton(int id, const char* text, int x, int y, int width, int height)
{
    return ::CreateWindowExA(
        0,
        "BUTTON",
        text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        x,
        y,
        width,
        height,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        instance_,
        nullptr);
}

void ModSettingsDialog::CreateControls()
{
    defaultFont_ = reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));

    CreateLabel("Mod name", 20, 18, 80, 20);
    nameEdit_ = CreateEdit(IDC_NAME_EDIT, 100, 16, 330, 23);
    statusLabel_ = CreateLabel("", 20, 44, 660, 18);

    settingsList_ = ::CreateWindowExA(
        WS_EX_CLIENTEDGE,
        WC_LISTVIEWA,
        "",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        20,
        72,
        420,
        265,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SETTINGS_LIST)),
        instance_,
        nullptr);
    ListView_SetExtendedListViewStyle(settingsList_, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    AddListViewColumn(settingsList_, 0, "Section", 125);
    AddListViewColumn(settingsList_, 1, "Key", 145);
    AddListViewColumn(settingsList_, 2, "Value", 130);

    sectionLabel_ = CreateLabel("Section", 462, 76, 70, 20);
    sectionEdit_ = CreateEdit(IDC_SECTION_EDIT, 462, 98, 210, 23);
    keyLabel_ = CreateLabel("Key", 462, 132, 70, 20);
    keyEdit_ = CreateEdit(IDC_KEY_EDIT, 462, 154, 210, 23);
    valueLabel_ = CreateLabel("Value", 462, 188, 70, 20);
    valueEdit_ = CreateEdit(IDC_VALUE_EDIT, 462, 210, 210, 23);

    applyButton_ = CreateButton(IDC_APPLY_SETTING, "Apply", 462, 252, 100, 28);
    addButton_ = CreateButton(IDC_ADD_SETTING, "Add", 572, 252, 100, 28);
    removeButton_ = CreateButton(IDC_REMOVE_SETTING, "Remove", 462, 290, 210, 28);

    rawEdit_ = ::CreateWindowExA(
        WS_EX_CLIENTEDGE,
        "EDIT",
        "",
        WS_CHILD | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL,
        20,
        72,
        652,
        265,
        window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RAW_EDIT)),
        instance_,
        nullptr);

    createButton_ = CreateButton(IDC_CREATE_INI, "Create INI", 20, 354, 120, 30);
    saveButton_ = CreateButton(IDC_SAVE_INI, "Save", 452, 354, 105, 30);
    closeButton_ = CreateButton(IDC_CLOSE_DIALOG, "Close", 567, 354, 105, 30);

    ApplyDefaultFont(window_);
}

void ModSettingsDialog::ApplyDefaultFont(HWND parent)
{
    HWND child = ::GetWindow(parent, GW_CHILD);
    while (child) {
        ::SendMessageA(child, WM_SETFONT, reinterpret_cast<WPARAM>(defaultFont_), TRUE);
        child = ::GetWindow(child, GW_HWNDNEXT);
    }
}

void ModSettingsDialog::LoadDocument()
{
    if (!config_ || modView_.type == ModType::Resource) {
        SetWindowTextString(statusLabel_, "Resource Mod: no DLL INI settings.");
        SetEditorMode(false, true);
        EnableWindow(createButton_, FALSE);
        EnableWindow(saveButton_, TRUE);
        return;
    }

    const ModEntry& mod = config_->mods[modView_.index];
    document_ = LoadModIniDocument(ReplaceExtension(mod.dllPath, ".ini"));

    if (!document_.exists) {
        SetWindowTextString(statusLabel_, "No INI found for " + mod.dllName + ".");
        SetEditorMode(false, true);
        return;
    }

    if (!document_.parseOk) {
        SetWindowTextString(statusLabel_, "Raw editor fallback: " + document_.path);
        SetWindowTextString(rawEdit_, document_.rawText);
        SetEditorMode(true, false);
        return;
    }

    SetWindowTextString(statusLabel_, "Editing " + document_.path);
    PopulateSettingsList();
    SetEditorMode(false, false);
}

void ModSettingsDialog::LoadName()
{
    if (!config_) {
        return;
    }

    if (modView_.type == ModType::Dll || modView_.type == ModType::DllDependency || modView_.type == ModType::SharedDll) {
        const ModEntry& mod = config_->mods[modView_.index];
        SetWindowTextString(nameEdit_, modView_.type == ModType::SharedDll ? GetSharedDllDisplayName(*config_, mod.dllName) : GetDllModDisplayName(mod));
    }
    else {
        SetWindowTextString(nameEdit_, GetResourceModDisplayName(config_->resourceMods[modView_.index]));
    }
}

void ModSettingsDialog::SetEditorMode(bool rawMode, bool missingFile)
{
    rawMode_ = rawMode;
    missingFile_ = missingFile;

    const int tableShow = (!rawMode && !missingFile) ? SW_SHOW : SW_HIDE;
    const int rawShow = rawMode ? SW_SHOW : SW_HIDE;

    ShowWindow(settingsList_, tableShow);
    ShowWindow(sectionLabel_, tableShow);
    ShowWindow(keyLabel_, tableShow);
    ShowWindow(valueLabel_, tableShow);
    ShowWindow(sectionEdit_, tableShow);
    ShowWindow(keyEdit_, tableShow);
    ShowWindow(valueEdit_, tableShow);
    ShowWindow(applyButton_, tableShow);
    ShowWindow(addButton_, tableShow);
    ShowWindow(removeButton_, tableShow);
    ShowWindow(rawEdit_, rawShow);

    EnableWindow(createButton_, missingFile ? TRUE : FALSE);
    EnableWindow(saveButton_, TRUE);
}

void ModSettingsDialog::PopulateSettingsList()
{
    ListView_DeleteAllItems(settingsList_);
    for (std::size_t i = 0; i < document_.entries.size(); ++i) {
        const ModIniEntry& entry = document_.entries[i];
        InsertListViewText(settingsList_, static_cast<int>(i), 0, entry.section);
        InsertListViewText(settingsList_, static_cast<int>(i), 1, entry.key);
        InsertListViewText(settingsList_, static_cast<int>(i), 2, entry.value);
    }
}

int ModSettingsDialog::GetSelectedSettingIndex() const
{
    return ListView_GetNextItem(settingsList_, -1, LVNI_SELECTED);
}

void ModSettingsDialog::LoadSettingControls()
{
    const int index = GetSelectedSettingIndex();
    if (index < 0 || index >= static_cast<int>(document_.entries.size())) {
        return;
    }

    const ModIniEntry& entry = document_.entries[static_cast<std::size_t>(index)];
    SetWindowTextString(sectionEdit_, entry.section);
    SetWindowTextString(keyEdit_, entry.key);
    SetWindowTextString(valueEdit_, entry.value);
}

bool ModSettingsDialog::ReadSettingControls(ModIniEntry* entry)
{
    if (!entry) {
        return false;
    }

    entry->section = Trim(GetWindowTextString(sectionEdit_));
    entry->key = Trim(GetWindowTextString(keyEdit_));
    entry->value = Trim(GetWindowTextString(valueEdit_));
    if (entry->key.empty()) {
        ShowError(window_, "Setting key cannot be empty.");
        return false;
    }

    return true;
}

void ModSettingsDialog::ApplySelectedSetting()
{
    const int index = GetSelectedSettingIndex();
    if (index < 0 || index >= static_cast<int>(document_.entries.size())) {
        return;
    }

    ModIniEntry entry;
    if (!ReadSettingControls(&entry)) {
        return;
    }

    document_.entries[static_cast<std::size_t>(index)] = entry;
    PopulateSettingsList();
    ListView_SetItemState(settingsList_, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void ModSettingsDialog::AddSetting()
{
    ModIniEntry entry;
    if (!ReadSettingControls(&entry)) {
        return;
    }

    document_.entries.push_back(entry);
    PopulateSettingsList();
    ListView_SetItemState(settingsList_, static_cast<int>(document_.entries.size() - 1), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
}

void ModSettingsDialog::RemoveSetting()
{
    const int index = GetSelectedSettingIndex();
    if (index < 0 || index >= static_cast<int>(document_.entries.size())) {
        return;
    }

    document_.entries.erase(document_.entries.begin() + index);
    PopulateSettingsList();
}

void ModSettingsDialog::SaveDocument()
{
    std::string error;
    if (config_) {
        const std::string newName = Trim(GetWindowTextString(nameEdit_));
        const ModMatch match{modView_.type, modView_.index};
        if (!RenameInstalledMod(config_, match, newName, &error)) {
            ShowError(window_, error);
            return;
        }
        if (!SaveLauncherConfig(GetLauncherIniPath(), *config_, &error)) {
            ShowError(window_, error);
            return;
        }
        configChanged_ = true;
    }

    if (!missingFile_ && !document_.path.empty() && (modView_.type == ModType::Dll || modView_.type == ModType::DllDependency || modView_.type == ModType::SharedDll)) {
        const std::string text = rawMode_ ? GetWindowTextString(rawEdit_) : SerializeModIniEntries(document_.entries);
        if (!WriteFileText(document_.path, text)) {
            ShowError(window_, "Failed to write " + document_.path);
            return;
        }
    }

    ShowInfo(window_, "Saved mod settings.");
    LoadDocument();
}

void ModSettingsDialog::CreateDocument()
{
    if (document_.path.empty()) {
        if (!config_ || (modView_.type != ModType::Dll && modView_.type != ModType::DllDependency && modView_.type != ModType::SharedDll)) {
            return;
        }
        document_.path = ReplaceExtension(config_->mods[modView_.index].dllPath, ".ini");
    }

    if (!WriteFileText(document_.path, "")) {
        ShowError(window_, "Failed to create " + document_.path);
        return;
    }

    LoadDocument();
}

void ModSettingsDialog::Close()
{
    ::DestroyWindow(window_);
}

void ModSettingsDialog::HandleCommand(int id)
{
    switch (id) {
    case IDC_APPLY_SETTING:
        ApplySelectedSetting();
        break;
    case IDC_ADD_SETTING:
        AddSetting();
        break;
    case IDC_REMOVE_SETTING:
        RemoveSetting();
        break;
    case IDC_CREATE_INI:
        CreateDocument();
        break;
    case IDC_SAVE_INI:
        SaveDocument();
        break;
    case IDC_CLOSE_DIALOG:
        Close();
        break;
    default:
        break;
    }
}

void ModSettingsDialog::HandleNotify(NMHDR* header)
{
    if (!header || header->code != LVN_ITEMCHANGED) {
        return;
    }

    NMLISTVIEW* listView = reinterpret_cast<NMLISTVIEW*>(header);
    if (header->idFrom == IDC_SETTINGS_LIST
        && (listView->uChanged & LVIF_STATE) != 0
        && (listView->uNewState & LVIS_SELECTED) != 0) {
        LoadSettingControls();
    }
}
}
