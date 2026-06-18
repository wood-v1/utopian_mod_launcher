#include "file_dialogs.h"

#include "../path_utils.h"

#include <commdlg.h>
#include <shldisp.h>
#include <shlobj.h>
#include <windowsx.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace uml::windows_ui
{
namespace
{
std::wstring ToWide(const std::string& value)
{
    if (value.empty()) {
        return std::wstring();
    }

    const int length = ::MultiByteToWideChar(CP_ACP, 0, value.c_str(), -1, nullptr, 0);
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    ::MultiByteToWideChar(CP_ACP, 0, value.c_str(), -1, &result[0], length);
    if (!result.empty() && result.back() == L'\0') {
        result.pop_back();
    }
    return result;
}

bool CreateDirectoryRecursive(const std::string& directory)
{
    if (directory.empty() || DirectoryExists(directory.c_str())) {
        return true;
    }

    const std::string parent = DirectoryName(directory);
    if (parent != directory && !DirectoryExists(parent.c_str()) && !CreateDirectoryRecursive(parent)) {
        return false;
    }

    return ::CreateDirectoryA(directory.c_str(), nullptr) != FALSE || ::GetLastError() == ERROR_ALREADY_EXISTS;
}

bool HasExtensionNoCase(const std::string& path, const char* extension)
{
    const std::size_t extensionLength = std::strlen(extension);
    return path.size() >= extensionLength
        && _stricmp(path.c_str() + path.size() - extensionLength, extension) == 0;
}

int CountFilesRecursive(const std::string& directory)
{
    const std::string pattern = JoinPath(directory, "*");
    WIN32_FIND_DATAA findData = {};
    HANDLE findHandle = ::FindFirstFileA(pattern.c_str(), &findData);
    if (findHandle == INVALID_HANDLE_VALUE) {
        return 0;
    }

    int count = 0;
    do {
        const std::string name = findData.cFileName;
        if (name == "." || name == "..") {
            continue;
        }

        const std::string fullPath = JoinPath(directory, name);
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            count += CountFilesRecursive(fullPath);
        }
        else {
            ++count;
        }
    } while (::FindNextFileA(findHandle, &findData));

    ::FindClose(findHandle);
    return count;
}

std::string QuoteArg(const std::string& value)
{
    std::string quoted = "\"";
    for (char ch : value) {
        if (ch == '"') {
            quoted += "\\\"";
        }
        else {
            quoted.push_back(ch);
        }
    }
    quoted += "\"";
    return quoted;
}

bool ReadRegistryString(HKEY root, const char* subkey, const char* valueName, std::string* value)
{
    HKEY key = nullptr;
    if (::RegOpenKeyExA(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }

    char buffer[MAX_PATH] = {};
    DWORD bufferSize = sizeof(buffer);
    DWORD type = 0;
    const LONG result = ::RegQueryValueExA(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &bufferSize);
    ::RegCloseKey(key);
    if (result != ERROR_SUCCESS || type != REG_SZ) {
        return false;
    }

    if (value) {
        *value = buffer;
    }
    return true;
}

std::vector<std::string> GetRarExtractorCandidates()
{
    std::vector<std::string> candidates;
    std::string installPath;
    if (ReadRegistryString(HKEY_LOCAL_MACHINE, "SOFTWARE\\WinRAR", "exe64", &installPath)
        || ReadRegistryString(HKEY_LOCAL_MACHINE, "SOFTWARE\\WinRAR", "exe32", &installPath)
        || ReadRegistryString(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\WinRAR", "exe64", &installPath)
        || ReadRegistryString(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\WinRAR", "exe32", &installPath)
        || ReadRegistryString(HKEY_CURRENT_USER, "SOFTWARE\\WinRAR", "exe64", &installPath)
        || ReadRegistryString(HKEY_CURRENT_USER, "SOFTWARE\\WinRAR", "exe32", &installPath)) {
        candidates.push_back(installPath);
        candidates.push_back(JoinPath(DirectoryName(installPath), "UnRAR.exe"));
    }

    candidates.push_back("C:\\Program Files\\WinRAR\\WinRAR.exe");
    candidates.push_back("C:\\Program Files\\WinRAR\\UnRAR.exe");
    candidates.push_back("C:\\Program Files (x86)\\WinRAR\\WinRAR.exe");
    candidates.push_back("C:\\Program Files (x86)\\WinRAR\\UnRAR.exe");
    return candidates;
}

bool RunExtractorProcess(const std::string& executable, const std::string& archivePath, const std::string& targetDirectory, std::string* error)
{
    const std::string exeName = FileNamePart(executable);
    const bool isUnrar = _stricmp(exeName.c_str(), "UnRAR.exe") == 0;
    std::string commandLine = QuoteArg(executable);
    commandLine += isUnrar ? " x -y " : " x -ibck -y ";
    commandLine += QuoteArg(archivePath);
    commandLine += " ";
    commandLine += QuoteArg(targetDirectory + "\\");

    STARTUPINFOA startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startupInfo.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION processInfo = {};

    std::vector<char> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back('\0');
    if (::CreateProcessA(
            nullptr,
            mutableCommand.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startupInfo,
            &processInfo) == FALSE) {
        if (error) {
            *error = "Failed to run RAR extractor: " + executable;
        }
        return false;
    }

    ::WaitForSingleObject(processInfo.hProcess, INFINITE);
    DWORD exitCode = 1;
    ::GetExitCodeProcess(processInfo.hProcess, &exitCode);
    ::CloseHandle(processInfo.hThread);
    ::CloseHandle(processInfo.hProcess);

    if (exitCode != 0) {
        if (error) {
            *error = "RAR extractor failed with exit code " + std::to_string(exitCode) + ": " + executable;
        }
        return false;
    }
    return true;
}

bool ExtractRarWithInstalledTool(const std::string& archivePath, const std::string& targetDirectory, std::string* error)
{
    if (!CreateDirectoryRecursive(targetDirectory)) {
        if (error) {
            *error = "Failed to create extraction directory: " + targetDirectory;
        }
        return false;
    }

    std::string lastError;
    for (const std::string& candidate : GetRarExtractorCandidates()) {
        if (!FileExists(candidate.c_str())) {
            continue;
        }
        if (RunExtractorProcess(candidate, archivePath, targetDirectory, &lastError)) {
            return true;
        }
    }

    if (error) {
        *error = lastError.empty()
            ? "RAR extraction is not available. Install WinRAR/UnRAR or repack the mod as .zip."
            : lastError;
    }
    return false;
}

void ReleaseVariant(VARIANT* value)
{
    if (value) {
        ::VariantClear(value);
    }
}

class StringChoiceDialog
{
public:
    StringChoiceDialog(HWND owner, const char* title, const std::vector<std::string>& items)
        : owner_(owner)
        , title_(title)
        , items_(items)
    {
    }

    bool Show(std::string* selectedItem)
    {
        WNDCLASSA windowClass = {};
        windowClass.lpfnWndProc = &StringChoiceDialog::WindowProcSetup;
        windowClass.hInstance = ::GetModuleHandleA(nullptr);
        windowClass.lpszClassName = "UtopianStringChoiceDialog";
        windowClass.hCursor = ::LoadCursorA(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        ::RegisterClassA(&windowClass);

        RECT parentRect = {};
        ::GetWindowRect(owner_, &parentRect);
        const int width = 360;
        const int height = 270;
        const int x = parentRect.left + ((parentRect.right - parentRect.left) - width) / 2;
        const int y = parentRect.top + ((parentRect.bottom - parentRect.top) - height) / 2;

        window_ = ::CreateWindowExA(
            WS_EX_DLGMODALFRAME,
            windowClass.lpszClassName,
            title_,
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
            return false;
        }

        ::EnableWindow(owner_, FALSE);
        ::ShowWindow(window_, SW_SHOW);
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
        if (accepted_ && selectedItem) {
            *selectedItem = selected_;
        }
        return accepted_;
    }

private:
    static LRESULT CALLBACK WindowProcSetup(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE) {
            CREATESTRUCTA* createStruct = reinterpret_cast<CREATESTRUCTA*>(lParam);
            StringChoiceDialog* self = reinterpret_cast<StringChoiceDialog*>(createStruct->lpCreateParams);
            ::SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            ::SetWindowLongPtrA(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&StringChoiceDialog::WindowProcThunk));
            self->window_ = window;
            return self->WindowProc(message, wParam, lParam);
        }

        return ::DefWindowProcA(window, message, wParam, lParam);
    }

    static LRESULT CALLBACK WindowProcThunk(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        StringChoiceDialog* self = reinterpret_cast<StringChoiceDialog*>(::GetWindowLongPtrA(window, GWLP_USERDATA));
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
            if (LOWORD(wParam) == 101 && HIWORD(wParam) == LBN_DBLCLK) {
                Accept();
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

    void CreateControls()
    {
        HFONT font = reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
        ::CreateWindowExA(0, "STATIC", "Choose DLL to add to LoadOrder:", WS_CHILD | WS_VISIBLE, 16, 16, 300, 20, window_, nullptr, ::GetModuleHandleA(nullptr), nullptr);
        list_ = ::CreateWindowExA(WS_EX_CLIENTEDGE, "LISTBOX", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | WS_VSCROLL, 16, 42, 312, 145, window_, reinterpret_cast<HMENU>(101), ::GetModuleHandleA(nullptr), nullptr);
        for (const std::string& item : items_) {
            ListBox_AddString(list_, item.c_str());
        }
        ListBox_SetCurSel(list_, 0);
        HWND ok = ::CreateWindowExA(0, "BUTTON", "OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 158, 198, 80, 28, window_, reinterpret_cast<HMENU>(IDOK), ::GetModuleHandleA(nullptr), nullptr);
        HWND cancel = ::CreateWindowExA(0, "BUTTON", "Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 248, 198, 80, 28, window_, reinterpret_cast<HMENU>(IDCANCEL), ::GetModuleHandleA(nullptr), nullptr);

        HWND child = ::GetWindow(window_, GW_CHILD);
        while (child) {
            ::SendMessageA(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            child = ::GetWindow(child, GW_HWNDNEXT);
        }
        ::SendMessageA(ok, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        ::SendMessageA(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }

    void Accept()
    {
        const int index = ListBox_GetCurSel(list_);
        if (index == LB_ERR) {
            return;
        }

        char buffer[MAX_PATH] = {};
        ListBox_GetText(list_, index, buffer);
        selected_ = buffer;
        accepted_ = true;
        ::DestroyWindow(window_);
    }

    HWND owner_ = nullptr;
    HWND window_ = nullptr;
    HWND list_ = nullptr;
    const char* title_ = "";
    const std::vector<std::string>& items_;
    bool accepted_ = false;
    std::string selected_;
};
}

bool PickFolder(HWND owner, const char* title, std::string* path)
{
    if (!path) {
        return false;
    }

    BROWSEINFOA browseInfo = {};
    browseInfo.hwndOwner = owner;
    browseInfo.lpszTitle = title;
    browseInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    PIDLIST_ABSOLUTE itemList = ::SHBrowseForFolderA(&browseInfo);
    if (!itemList) {
        return false;
    }

    char buffer[MAX_PATH] = {};
    const bool ok = ::SHGetPathFromIDListA(itemList, buffer) != FALSE;
    ::CoTaskMemFree(itemList);
    if (!ok) {
        return false;
    }

    *path = buffer;
    return true;
}

bool PickZipFile(HWND owner, std::string* path)
{
    if (!path) {
        return false;
    }

    char fileName[32768] = {};
    OPENFILENAMEA openFileName = {};
    openFileName.lStructSize = sizeof(openFileName);
    openFileName.hwndOwner = owner;
    openFileName.lpstrFilter = "Zip Archives (*.zip)\0*.zip\0All Files (*.*)\0*.*\0";
    openFileName.lpstrFile = fileName;
    openFileName.nMaxFile = sizeof(fileName);
    openFileName.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!::GetOpenFileNameA(&openFileName)) {
        const DWORD dialogError = ::CommDlgExtendedError();
        if (dialogError != 0) {
            ::MessageBoxA(owner, ("Failed to open file picker. Common Dialog error: " + std::to_string(dialogError)).c_str(), "UtopianModLauncher", MB_ICONERROR | MB_OK);
        }
        return false;
    }

    *path = fileName;
    return true;
}

bool PickModPackageFile(HWND owner, std::string* path)
{
    if (!path) {
        return false;
    }

    char fileName[32768] = {};
    OPENFILENAMEA openFileName = {};
    openFileName.lStructSize = sizeof(openFileName);
    openFileName.hwndOwner = owner;
    openFileName.lpstrFilter = "Mod Archives (*.zip;*.rar)\0*.zip;*.rar\0Zip Archives (*.zip)\0*.zip\0RAR Archives (*.rar)\0*.rar\0All Files (*.*)\0*.*\0";
    openFileName.lpstrFile = fileName;
    openFileName.nMaxFile = sizeof(fileName);
    openFileName.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!::GetOpenFileNameA(&openFileName)) {
        const DWORD dialogError = ::CommDlgExtendedError();
        if (dialogError != 0) {
            ::MessageBoxA(owner, ("Failed to open file picker. Common Dialog error: " + std::to_string(dialogError)).c_str(), "UtopianModLauncher", MB_ICONERROR | MB_OK);
        }
        return false;
    }

    *path = fileName;
    return true;
}

bool PickStringFromList(HWND owner, const char* title, const std::vector<std::string>& items, std::string* selectedItem)
{
    if (items.empty() || !selectedItem) {
        return false;
    }

    if (items.size() == 1) {
        *selectedItem = items.front();
        return true;
    }

    StringChoiceDialog dialog(owner, title, items);
    return dialog.Show(selectedItem);
}

bool CreateTempDirectory(std::string* path, std::string* error)
{
    if (!path) {
        return false;
    }

    char tempPath[MAX_PATH] = {};
    char tempFile[MAX_PATH] = {};
    if (::GetTempPathA(MAX_PATH, tempPath) == 0 || ::GetTempFileNameA(tempPath, "uml", 0, tempFile) == 0) {
        if (error) {
            *error = "Failed to allocate a temporary directory.";
        }
        return false;
    }

    ::DeleteFileA(tempFile);
    if (::CreateDirectoryA(tempFile, nullptr) == FALSE) {
        if (error) {
            *error = "Failed to create temporary directory.";
        }
        return false;
    }

    *path = tempFile;
    return true;
}

bool ExtractZipToDirectory(const std::string& zipPath, const std::string& targetDirectory, std::string* error)
{
    if (!CreateDirectoryRecursive(targetDirectory)) {
        if (error) {
            *error = "Failed to create extraction directory: " + targetDirectory;
        }
        return false;
    }

    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool shouldUninitialize = SUCCEEDED(hr);
    if (hr == RPC_E_CHANGED_MODE) {
        hr = S_OK;
    }
    if (FAILED(hr)) {
        if (error) {
            *error = "Failed to initialize COM for zip extraction.";
        }
        return false;
    }

    IShellDispatch* shell = nullptr;
    hr = ::CoCreateInstance(CLSID_Shell, nullptr, CLSCTX_INPROC_SERVER, IID_IShellDispatch, reinterpret_cast<void**>(&shell));
    if (FAILED(hr) || !shell) {
        if (shouldUninitialize) {
            ::CoUninitialize();
        }
        if (error) {
            *error = "Failed to create Shell COM object.";
        }
        return false;
    }

    VARIANT zipVariant = {};
    VARIANT targetVariant = {};
    zipVariant.vt = VT_BSTR;
    targetVariant.vt = VT_BSTR;
    zipVariant.bstrVal = ::SysAllocString(ToWide(zipPath).c_str());
    targetVariant.bstrVal = ::SysAllocString(ToWide(targetDirectory).c_str());

    Folder* zipFolder = nullptr;
    Folder* targetFolder = nullptr;
    hr = shell->NameSpace(zipVariant, &zipFolder);
    if (SUCCEEDED(hr)) {
        hr = shell->NameSpace(targetVariant, &targetFolder);
    }

    bool ok = false;
    if (SUCCEEDED(hr) && zipFolder && targetFolder) {
        FolderItems* items = nullptr;
        hr = zipFolder->Items(&items);
        if (SUCCEEDED(hr) && items) {
            VARIANT itemsVariant = {};
            VARIANT optionsVariant = {};
            itemsVariant.vt = VT_DISPATCH;
            itemsVariant.pdispVal = items;
            items->AddRef();
            optionsVariant.vt = VT_I4;
            optionsVariant.lVal = 4 | 16 | 1024;

            hr = targetFolder->CopyHere(itemsVariant, optionsVariant);
            ok = SUCCEEDED(hr);
            ReleaseVariant(&itemsVariant);
            ReleaseVariant(&optionsVariant);
            items->Release();
        }
    }

    if (zipFolder) {
        zipFolder->Release();
    }
    if (targetFolder) {
        targetFolder->Release();
    }
    ReleaseVariant(&zipVariant);
    ReleaseVariant(&targetVariant);
    shell->Release();
    if (shouldUninitialize) {
        ::CoUninitialize();
    }

    if (!ok) {
        if (error) {
            *error = "Failed to extract zip archive.";
        }
        return false;
    }

    int lastCount = -1;
    int stableTicks = 0;
    for (int i = 0; i < 100; ++i) {
        const int count = CountFilesRecursive(targetDirectory);
        if (count > 0 && count == lastCount) {
            ++stableTicks;
            if (stableTicks >= 5) {
                return true;
            }
        }
        else {
            stableTicks = 0;
            lastCount = count;
        }

        ::Sleep(100);
    }

    if (CountFilesRecursive(targetDirectory) > 0) {
        return true;
    }

    if (error) {
        *error = "Archive extraction finished, but no files were found. The archive may be empty, locked, or unsupported by Windows Shell.";
    }
    return false;
}

bool ExtractArchiveToDirectory(const std::string& archivePath, const std::string& targetDirectory, std::string* error)
{
    std::string shellError;
    if (ExtractZipToDirectory(archivePath, targetDirectory, &shellError)) {
        return true;
    }

    if (HasExtensionNoCase(archivePath, ".rar")) {
        std::string rarError;
        if (ExtractRarWithInstalledTool(archivePath, targetDirectory, &rarError)) {
            return true;
        }
        if (error) {
            *error = "Failed to extract RAR archive.\n\nShell extraction: " + shellError + "\nWinRAR/UnRAR: " + rarError;
        }
        return false;
    }

    if (error) {
        *error = shellError;
    }
    return false;
}
}
