#pragma once

#include "../launcher_types.h"

#include <windows.h>

#include <string>

namespace uml::windows_ui
{
class ModSettingsDialog
{
public:
    ModSettingsDialog(HINSTANCE instance, HWND parent, LauncherConfig* config, const InstalledModView& modView);
    bool ShowModal();

private:
    static LRESULT CALLBACK WindowProcSetup(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WindowProcThunk(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
    HWND CreateLabel(const char* text, int x, int y, int width, int height);
    HWND CreateEdit(int id, int x, int y, int width, int height, DWORD extraStyle = 0);
    HWND CreateButton(int id, const char* text, int x, int y, int width, int height);
    void CreateControls();
    void ApplyDefaultFont(HWND parent);
    void LoadDocument();
    void LoadName();
    void SetEditorMode(bool rawMode, bool missingFile);
    void PopulateSettingsList();
    int GetSelectedSettingIndex() const;
    void LoadSettingControls();
    bool ReadSettingControls(ModIniEntry* entry);
    void ApplySelectedSetting();
    void AddSetting();
    void RemoveSetting();
    void SaveDocument();
    void CreateDocument();
    void Close();
    void HandleCommand(int id);
    void HandleNotify(NMHDR* header);

    HINSTANCE instance_ = nullptr;
    HWND parent_ = nullptr;
    HWND window_ = nullptr;
    HFONT defaultFont_ = nullptr;

    LauncherConfig* config_ = nullptr;
    InstalledModView modView_;
    ModIniDocument document_;
    bool rawMode_ = false;
    bool missingFile_ = false;
    bool configChanged_ = false;

    HWND nameEdit_ = nullptr;
    HWND statusLabel_ = nullptr;
    HWND settingsList_ = nullptr;
    HWND sectionLabel_ = nullptr;
    HWND keyLabel_ = nullptr;
    HWND valueLabel_ = nullptr;
    HWND sectionEdit_ = nullptr;
    HWND keyEdit_ = nullptr;
    HWND valueEdit_ = nullptr;
    HWND rawEdit_ = nullptr;
    HWND applyButton_ = nullptr;
    HWND addButton_ = nullptr;
    HWND removeButton_ = nullptr;
    HWND createButton_ = nullptr;
    HWND saveButton_ = nullptr;
    HWND closeButton_ = nullptr;
};
}
