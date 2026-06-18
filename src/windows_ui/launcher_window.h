#pragma once

#include "../launcher_types.h"
#include "../launcher_services.h"

#include <windows.h>

namespace uml::windows_ui
{
struct InstalledFilesText;

class LauncherWindow
{
public:
    explicit LauncherWindow(HINSTANCE instance);
    int Run();

private:
    static LRESULT CALLBACK WindowProcSetup(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WindowProcThunk(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
    HWND CreateLabel(const char* text, int x, int y, int width, int height);
    HWND CreateEdit(int id, int x, int y, int width, int height, DWORD extraStyle = 0, DWORD exStyle = WS_EX_CLIENTEDGE);
    HWND CreateButton(int id, const char* text, int x, int y, int width, int height, DWORD style = 0);
    HWND CreateGroup(const char* text, int x, int y, int width, int height);
    void CreateControls();
    void ApplyDefaultFont(HWND parent);
    void LoadBanner();
    void LoadControlsFromConfig();
    void ReadConfigFromControls();
    void ReloadConfigAndList(const std::string& selectName);
    void RefreshConflictCache();
    void PopulateModList();
    void SelectMod(int index);
    int GetSelectedModIndex() const;
    void LoadSelectedModControls();
    void ApplySelectedModControls(bool showErrors);
    void SetBusy(bool busy);
    void BeginBusy(const std::string& message);
    void EndBusy();
    void UpdateBusyIndicator();
    void UpdateActionState();
    void MoveSelectedMod(int direction);
    bool SaveConfigFromUi();
    void LaunchFromUi();
    void OpenSelectedModSettings();
    void ShowSelectedModInstalledFiles(bool updateStatus);
    void StartInstalledFilesLoading(const InstalledModView& view, bool updateStatus);
    void FinishInstalledFilesLoading(int requestId, InstalledFilesText* installedFiles);
    void UpdateInstalledFilesLoadingIndicator();
    void InstallMod();
    void StartArchivePreparation(const std::string& archivePath);
    void FinishArchivePreparation(int requestId, void* payload);
    void InstallModFromPackage(const std::string& packageRoot, const std::string& defaultName);
    void StartPackageAnalysis(const std::string& packageRoot, const std::string& defaultName, const std::string& packageTargetRelativeDirectory);
    void FinishPackageAnalysis(int requestId, void* payload);
    void ContinueInstallWithPackageFiles(
        const std::string& packageRoot,
        const std::string& defaultName,
        const std::string& packageTargetRelativeDirectory,
        const std::vector<PackageFile>& files);
    void StartPackageInstall(const InstallModOptions& options);
    void FinishPackageInstall(int requestId, void* payload);
    void DeleteSelectedMod();
    void StartModDelete(const ModMatch& match, const std::string& displayName, int selectedIndex);
    void FinishModDelete(int requestId, void* payload);
    void StartBackupsAudit();
    void FinishBackupsAudit(int requestId, void* payload);
    void HandleCommand(int id, int notification);
    LRESULT HandleNotify(NMHDR* header);

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HFONT defaultFont_ = nullptr;
    HFONT launchFont_ = nullptr;
    HFONT titleFont_ = nullptr;
    HBITMAP bannerBitmap_ = nullptr;

    LauncherConfig config_;
    std::vector<InstalledModView> modViews_;
    std::vector<ModConflictEntry> conflictCache_;
    int selectedViewIndex_ = -1;
    bool updatingControls_ = false;
    bool busy_ = false;
    bool installedFilesLoading_ = false;
    int installedFilesRequestId_ = 0;
    int installedFilesSpinnerFrame_ = 0;
    bool busySpinnerActive_ = false;
    int busyRequestId_ = 0;
    int busySpinnerFrame_ = 0;
    std::string busyMessage_;

    HWND bannerControl_ = nullptr;
    HWND titleLabel_ = nullptr;
    HWND statusLabel_ = nullptr;
    HWND installedFilesEdit_ = nullptr;
    HWND loggingCheck_ = nullptr;
    HWND helpButton_ = nullptr;
    HWND backupsButton_ = nullptr;
    HWND modList_ = nullptr;
    HWND stageCombo_ = nullptr;
    HWND stageHelpButton_ = nullptr;
    HWND delayEdit_ = nullptr;
    HWND moveUpButton_ = nullptr;
    HWND moveDownButton_ = nullptr;
    HWND settingsButton_ = nullptr;
    HWND installButton_ = nullptr;
    HWND deleteButton_ = nullptr;
    HWND saveButton_ = nullptr;
    HWND launchButton_ = nullptr;
};
}
