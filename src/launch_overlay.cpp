#include "launch_overlay.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace uml
{
namespace
{
constexpr const char* kOverlayClassName = "UtopianLaunchOverlayWindow";
constexpr UINT_PTR kOverlayTimerId = 1;
constexpr UINT kOverlayTickMs = 250;
constexpr DWORD kWaitingOverlayMs = 120000;
constexpr DWORD kVisibleOverlayMs = 10000;
constexpr int kOverlayWidth = 440;
constexpr int kOverlayHeight = 152;
constexpr int kOverlayMargin = 18;
constexpr std::size_t kMaxSummaryItems = 10;
constexpr std::size_t kMaxSummaryNameLength = 10;

struct OverlayState
{
    uint32_t processId = 0;
    HANDLE processHandle = nullptr;
    HWND gameWindow = nullptr;
    LaunchOverlayInfo info;
    LaunchOverlayProgress progress;
    std::string statusPath;
    DWORD startedAt = 0;
    DWORD expiresAt = 0;
    bool finalCountdownStarted = false;
    HFONT font = nullptr;
    HFONT titleFont = nullptr;
};

struct WindowSearch
{
    uint32_t processId = 0;
    HWND window = nullptr;
};

BOOL CALLBACK FindMainWindowProc(HWND window, LPARAM parameter)
{
    WindowSearch* search = reinterpret_cast<WindowSearch*>(parameter);
    DWORD windowProcessId = 0;
    ::GetWindowThreadProcessId(window, &windowProcessId);
    if (windowProcessId != search->processId) {
        return TRUE;
    }

    if (!::IsWindowVisible(window) || ::GetWindow(window, GW_OWNER) != nullptr) {
        return TRUE;
    }

    RECT rect = {};
    if (!::GetWindowRect(window, &rect) || rect.right <= rect.left || rect.bottom <= rect.top) {
        return TRUE;
    }

    search->window = window;
    return FALSE;
}

HWND FindMainWindow(uint32_t processId)
{
    WindowSearch search;
    search.processId = processId;
    ::EnumWindows(FindMainWindowProc, reinterpret_cast<LPARAM>(&search));
    return search.window;
}

std::string SanitizeStatusValue(const std::string& value)
{
    std::string sanitized;
    sanitized.reserve(value.size());
    for (char ch : value) {
        if (ch == '\r' || ch == '\n') {
            sanitized.push_back(' ');
        }
        else {
            sanitized.push_back(ch);
        }
    }
    return sanitized;
}

std::string SanitizeStatusListValue(const std::string& value)
{
    std::string sanitized;
    sanitized.reserve(value.size());
    for (char ch : value) {
        if (ch == '\r' || ch == '\n' || ch == '|' || ch == '\t') {
            sanitized.push_back(' ');
        }
        else {
            sanitized.push_back(ch);
        }
    }
    return sanitized;
}

std::string JoinStatusList(const std::vector<std::string>& values)
{
    std::string joined;
    for (const std::string& value : values) {
        if (!joined.empty()) {
            joined.push_back('|');
        }
        joined += SanitizeStatusListValue(value);
    }
    return joined;
}

std::vector<std::string> SplitStatusList(const std::string& value)
{
    std::vector<std::string> values;
    std::string current;
    for (char ch : value) {
        if (ch == '|') {
            values.push_back(current);
            current.clear();
        }
        else {
            current.push_back(ch);
        }
    }
    if (!current.empty() || !value.empty()) {
        values.push_back(current);
    }
    return values;
}

std::map<std::string, std::string> ReadStatusFile(const std::string& path)
{
    std::map<std::string, std::string> values;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        values[line.substr(0, separator)] = line.substr(separator + 1);
    }
    return values;
}

uint32_t ParseUInt(const std::map<std::string, std::string>& values, const char* key, uint32_t fallback)
{
    const auto found = values.find(key);
    if (found == values.end()) {
        return fallback;
    }
    return static_cast<uint32_t>(std::strtoul(found->second.c_str(), nullptr, 10));
}

bool ParseBool(const std::map<std::string, std::string>& values, const char* key, bool fallback)
{
    const auto found = values.find(key);
    if (found == values.end()) {
        return fallback;
    }
    return found->second == "1";
}

void RefreshProgress(OverlayState* state)
{
    if (state == nullptr || state->statusPath.empty()) {
        return;
    }

    const std::map<std::string, std::string> values = ReadStatusFile(state->statusPath);
    if (values.empty()) {
        return;
    }

    const auto current = values.find("current");
    if (current != values.end()) {
        state->progress.current = current->second;
    }
    const auto dllNames = values.find("dllNames");
    if (dllNames != values.end()) {
        state->progress.dllModNames = SplitStatusList(dllNames->second);
    }
    const auto resourceNames = values.find("resourceNames");
    if (resourceNames != values.end()) {
        state->progress.resourceModNames = SplitStatusList(resourceNames->second);
    }
    state->progress.completedCount = ParseUInt(values, "completed", state->progress.completedCount);
    state->progress.totalCount = ParseUInt(values, "total", state->progress.totalCount);
    state->progress.finished = ParseBool(values, "finished", state->progress.finished);
    state->progress.failed = ParseBool(values, "failed", state->progress.failed);

    if ((state->progress.finished || state->progress.failed) && !state->finalCountdownStarted) {
        state->expiresAt = ::GetTickCount() + kVisibleOverlayMs;
        state->finalCountdownStarted = true;
    }
}

void UpdateOverlayPosition(HWND overlay, HWND gameWindow)
{
    RECT gameRect = {};
    if (::IsWindow(gameWindow) && ::GetWindowRect(gameWindow, &gameRect)) {
        ::SetWindowPos(
            overlay,
            HWND_TOPMOST,
            gameRect.left + kOverlayMargin,
            gameRect.top + kOverlayMargin,
            kOverlayWidth,
            kOverlayHeight,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        return;
    }

    RECT workArea = {};
    if (!::SystemParametersInfoA(SPI_GETWORKAREA, 0, &workArea, 0)) {
        workArea.left = 0;
        workArea.top = 0;
    }
    ::SetWindowPos(
        overlay,
        HWND_TOPMOST,
        workArea.left + kOverlayMargin,
        workArea.top + kOverlayMargin,
        kOverlayWidth,
        kOverlayHeight,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void DrawTextLine(HDC dc, const std::string& text, int left, int top, int right)
{
    RECT textRect = {left, top, right, top + 24};
    ::DrawTextA(
        dc,
        text.c_str(),
        static_cast<int>(text.size()),
        &textRect,
        DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
}

std::string CompactName(const std::string& name)
{
    if (name.size() <= kMaxSummaryNameLength) {
        return name;
    }
    return name.substr(0, kMaxSummaryNameLength - 3) + "...";
}

std::string BuildSummaryList(
    const char* label,
    const std::vector<std::string>& names,
    std::size_t* remainingSlots)
{
    if (remainingSlots == nullptr || *remainingSlots == 0 || names.empty()) {
        return std::string(label) + ": -";
    }

    std::string line = std::string(label) + ": ";
    std::size_t added = 0;
    for (const std::string& name : names) {
        if (*remainingSlots == 0) {
            break;
        }
        if (added != 0) {
            line += ", ";
        }
        line += CompactName(name);
        ++added;
        --(*remainingSlots);
    }

    if (added < names.size()) {
        line += ", +" + std::to_string(names.size() - added);
    }

    return line;
}

void DrawOverlay(HWND window, OverlayState* state)
{
    PAINTSTRUCT paint = {};
    HDC dc = ::BeginPaint(window, &paint);
    RECT rect = {};
    ::GetClientRect(window, &rect);

    HBRUSH background = ::CreateSolidBrush(RGB(28, 22, 18));
    ::FillRect(dc, &rect, background);
    ::DeleteObject(background);

    HPEN border = ::CreatePen(PS_SOLID, 1, RGB(216, 132, 54));
    HGDIOBJ oldPen = ::SelectObject(dc, border);
    HGDIOBJ oldBrush = ::SelectObject(dc, ::GetStockObject(NULL_BRUSH));
    ::Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
    ::SelectObject(dc, oldBrush);
    ::SelectObject(dc, oldPen);
    ::DeleteObject(border);

    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, RGB(248, 230, 205));

    if (state->titleFont != nullptr) {
        ::SelectObject(dc, state->titleFont);
    }

    const std::string title = state->progress.failed
        ? "Utopian Launcher: launch failed"
        : (state->progress.finished ? "Launched with Utopian Launcher " + state->info.version : "Utopian Launcher is loading mods");
    DrawTextLine(dc, title, 16, 12, rect.right - 16);

    if (state->font != nullptr) {
        ::SelectObject(dc, state->font);
    }

    if (state->progress.finished && !state->progress.failed) {
        const std::string counts = "Installed "
            + std::to_string(state->info.dllModCount)
            + " DLL Mods, "
            + std::to_string(state->info.resourceModCount)
            + " Resource Mods";
        std::size_t remainingSlots = kMaxSummaryItems;
        const std::string dllNames = BuildSummaryList("DLL", state->progress.dllModNames, &remainingSlots);
        const std::string resourceNames = BuildSummaryList("RES", state->progress.resourceModNames, &remainingSlots);
        DrawTextLine(dc, counts, 16, 42, rect.right - 16);
        DrawTextLine(dc, dllNames, 16, 72, rect.right - 16);
        DrawTextLine(dc, resourceNames, 16, 100, rect.right - 16);
    }
    else {
        const uint32_t remaining = state->progress.totalCount > state->progress.completedCount
            ? state->progress.totalCount - state->progress.completedCount
            : 0;
        const std::string progress = "DLL mods: "
            + std::to_string(state->progress.completedCount)
            + "/"
            + std::to_string(state->progress.totalCount)
            + " loaded, "
            + std::to_string(remaining)
            + " remaining";
        const std::string current = state->progress.current.empty() ? "Starting game..." : state->progress.current;
        DrawTextLine(dc, progress, 16, 44, rect.right - 16);
        DrawTextLine(dc, current, 16, 72, rect.right - 16);
    }

    ::EndPaint(window, &paint);
}

LRESULT CALLBACK OverlayWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    OverlayState* state = reinterpret_cast<OverlayState*>(::GetWindowLongPtrA(window, GWLP_USERDATA));

    switch (message) {
    case WM_NCCREATE:
    {
        CREATESTRUCTA* create = reinterpret_cast<CREATESTRUCTA*>(lParam);
        state = reinterpret_cast<OverlayState*>(create->lpCreateParams);
        ::SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
    }
    case WM_CREATE:
        if (state != nullptr) {
            state->titleFont = ::CreateFontA(
                19,
                0,
                0,
                0,
                FW_SEMIBOLD,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_SWISS,
                "Segoe UI");
            state->font = ::CreateFontA(
                18,
                0,
                0,
                0,
                FW_NORMAL,
                FALSE,
                FALSE,
                FALSE,
                DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY,
                DEFAULT_PITCH | FF_SWISS,
                "Segoe UI");
            ::SetTimer(window, kOverlayTimerId, kOverlayTickMs, nullptr);
        }
        return 0;
    case WM_TIMER:
    {
        if (state == nullptr) {
            ::DestroyWindow(window);
            return 0;
        }

        RefreshProgress(state);
        if (state->gameWindow == nullptr || !::IsWindow(state->gameWindow)) {
            state->gameWindow = FindMainWindow(state->processId);
        }

        const DWORD now = ::GetTickCount();
        if (state->processHandle != nullptr
            && ::WaitForSingleObject(state->processHandle, 0) == WAIT_OBJECT_0) {
            ::DestroyWindow(window);
            return 0;
        }

        if ((state->finalCountdownStarted && now >= state->expiresAt)
            || (!state->finalCountdownStarted && now - state->startedAt >= kWaitingOverlayMs)) {
            ::DestroyWindow(window);
            return 0;
        }

        UpdateOverlayPosition(window, state->gameWindow);
        ::InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_PAINT:
        if (state != nullptr) {
            DrawOverlay(window, state);
            return 0;
        }
        break;
    case WM_NCDESTROY:
        if (state != nullptr && state->font != nullptr) {
            ::DeleteObject(state->font);
            state->font = nullptr;
        }
        if (state != nullptr && state->titleFont != nullptr) {
            ::DeleteObject(state->titleFont);
            state->titleFont = nullptr;
        }
        if (state != nullptr && state->processHandle != nullptr) {
            ::CloseHandle(state->processHandle);
            state->processHandle = nullptr;
        }
        ::SetWindowLongPtrA(window, GWLP_USERDATA, 0);
        ::PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    return ::DefWindowProcA(window, message, wParam, lParam);
}

bool RegisterOverlayClass(HINSTANCE instance)
{
    WNDCLASSA windowClass = {};
    windowClass.lpfnWndProc = OverlayWindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kOverlayClassName;
    windowClass.hCursor = ::LoadCursorA(nullptr, IDC_ARROW);

    if (::RegisterClassA(&windowClass) != 0) {
        return true;
    }

    return ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

std::string QuoteCommandArgument(const std::string& value)
{
    std::string quoted = "\"";
    for (char ch : value) {
        if (ch == '"' || ch == '\\') {
            quoted.push_back('\\');
        }
        quoted.push_back(ch);
    }
    quoted.push_back('"');
    return quoted;
}

int RunOverlayWindow(uint32_t processId, LaunchOverlayInfo info, const std::string& statusPath)
{
    HINSTANCE instance = ::GetModuleHandleA(nullptr);
    if (!RegisterOverlayClass(instance)) {
        return 1;
    }

    OverlayState state;
    state.processId = processId;
    state.processHandle = ::OpenProcess(SYNCHRONIZE, FALSE, processId);
    state.gameWindow = FindMainWindow(processId);
    state.info = std::move(info);
    state.statusPath = statusPath;
    state.progress.totalCount = state.info.dllModCount;
    state.startedAt = ::GetTickCount();
    state.expiresAt = state.startedAt + kWaitingOverlayMs;
    RefreshProgress(&state);

    HWND overlay = ::CreateWindowExA(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        kOverlayClassName,
        "",
        WS_POPUP,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kOverlayWidth,
        kOverlayHeight,
        nullptr,
        nullptr,
        instance,
        &state);
    if (overlay == nullptr) {
        return 1;
    }

    ::SetLayeredWindowAttributes(overlay, 0, 226, LWA_ALPHA);
    UpdateOverlayPosition(overlay, state.gameWindow);
    ::ShowWindow(overlay, SW_SHOWNOACTIVATE);
    ::UpdateWindow(overlay);

    MSG message = {};
    while (::GetMessageA(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageA(&message);
    }

    return 0;
}
}

std::string GetLaunchOverlayStatusPath(uint32_t processId)
{
    char tempPath[MAX_PATH] = {};
    const DWORD length = ::GetTempPathA(MAX_PATH, tempPath);
    const std::string directory = length > 0 && length < MAX_PATH ? tempPath : ".\\";
    return directory + "UtopianLaunchOverlay_" + std::to_string(processId) + ".status";
}

void WriteLaunchOverlayProgress(const std::string& statusPath, const LaunchOverlayProgress& progress)
{
    if (statusPath.empty()) {
        return;
    }

    const std::string tempPath = statusPath + ".tmp";
    {
        std::ofstream output(tempPath, std::ios::trunc);
        if (!output) {
            return;
        }
        output << "current=" << SanitizeStatusValue(progress.current) << "\n";
        if (!progress.dllModNames.empty()) {
            output << "dllNames=" << JoinStatusList(progress.dllModNames) << "\n";
        }
        if (!progress.resourceModNames.empty()) {
            output << "resourceNames=" << JoinStatusList(progress.resourceModNames) << "\n";
        }
        output << "completed=" << progress.completedCount << "\n";
        output << "total=" << progress.totalCount << "\n";
        output << "finished=" << (progress.finished ? 1 : 0) << "\n";
        output << "failed=" << (progress.failed ? 1 : 0) << "\n";
    }

    if (!::MoveFileExA(tempPath.c_str(), statusPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ::DeleteFileA(tempPath.c_str());
    }
}

void DeleteLaunchOverlayStatus(const std::string& statusPath)
{
    if (!statusPath.empty()) {
        ::DeleteFileA(statusPath.c_str());
        ::DeleteFileA((statusPath + ".tmp").c_str());
    }
}

void ShowLaunchOverlayForProcess(uint32_t processId, LaunchOverlayInfo info, const std::string& statusPath)
{
    char modulePath[MAX_PATH] = {};
    if (::GetModuleFileNameA(nullptr, modulePath, MAX_PATH) == 0) {
        return;
    }

    std::string commandLine = QuoteCommandArgument(modulePath)
        + " --launch-overlay "
        + std::to_string(processId)
        + " "
        + std::to_string(info.dllModCount)
        + " "
        + std::to_string(info.resourceModCount)
        + " "
        + QuoteCommandArgument(statusPath);

    STARTUPINFOA startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};
    if (!::CreateProcessA(
        modulePath,
        &commandLine[0],
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo)) {
        return;
    }

    ::CloseHandle(processInfo.hThread);
    ::CloseHandle(processInfo.hProcess);
}

int RunLaunchOverlayProcess(uint32_t processId, LaunchOverlayInfo info, const std::string& statusPath)
{
    const int result = RunOverlayWindow(processId, std::move(info), statusPath);
    DeleteLaunchOverlayStatus(statusPath);
    return result;
}
}
