#include "launch_overlay.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

namespace uml
{
namespace
{
constexpr const char* kOverlayClassName = "UtopianLaunchOverlayWindow";
constexpr UINT_PTR kOverlayTimerId = 1;
constexpr UINT kOverlayTickMs = 250;
constexpr DWORD kFindWindowTimeoutMs = 10000;
constexpr DWORD kVisibleOverlayMs = 10000;
constexpr int kOverlayWidth = 380;
constexpr int kOverlayHeight = 92;
constexpr int kOverlayMargin = 18;

struct OverlayState
{
    uint32_t processId = 0;
    HWND gameWindow = nullptr;
    LaunchOverlayInfo info;
    DWORD expiresAt = 0;
    HFONT font = nullptr;
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

HWND WaitForMainWindow(uint32_t processId)
{
    const DWORD startedAt = ::GetTickCount();
    while (::GetTickCount() - startedAt < kFindWindowTimeoutMs) {
        HWND window = FindMainWindow(processId);
        if (window != nullptr) {
            return window;
        }
        ::Sleep(250);
    }
    return nullptr;
}

void UpdateOverlayPosition(HWND overlay, HWND gameWindow)
{
    RECT gameRect = {};
    if (!::IsWindow(gameWindow) || !::GetWindowRect(gameWindow, &gameRect)) {
        return;
    }

    ::SetWindowPos(
        overlay,
        HWND_TOPMOST,
        gameRect.left + kOverlayMargin,
        gameRect.top + kOverlayMargin,
        kOverlayWidth,
        kOverlayHeight,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
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

    if (state->font != nullptr) {
        ::SelectObject(dc, state->font);
    }

    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, RGB(248, 230, 205));

    const std::string title = "Launched with Utopian Launcher " + state->info.version;
    const std::string dllMods = "Installed " + std::to_string(state->info.dllModCount) + " DLL Mods";
    const std::string resourceMods = "Installed " + std::to_string(state->info.resourceModCount) + " Resource Mods";

    ::TextOutA(dc, 16, 14, title.c_str(), static_cast<int>(title.size()));
    ::TextOutA(dc, 16, 38, dllMods.c_str(), static_cast<int>(dllMods.size()));
    ::TextOutA(dc, 16, 62, resourceMods.c_str(), static_cast<int>(resourceMods.size()));

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
            state->font = ::CreateFontA(
                16,
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
        if (state == nullptr || !::IsWindow(state->gameWindow) || ::GetTickCount() >= state->expiresAt) {
            ::DestroyWindow(window);
            return 0;
        }
        UpdateOverlayPosition(window, state->gameWindow);
        return 0;
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

int RunOverlayWindow(uint32_t processId, LaunchOverlayInfo info)
{
    HWND gameWindow = WaitForMainWindow(processId);
    if (gameWindow == nullptr) {
        return 0;
    }

    // Give the freshly-created game window a moment to finish its first paint.
    ::Sleep(1500);

    HINSTANCE instance = ::GetModuleHandleA(nullptr);
    if (!RegisterOverlayClass(instance)) {
        return 1;
    }

    OverlayState state;
    state.processId = processId;
    state.gameWindow = gameWindow;
    state.info = std::move(info);
    state.expiresAt = ::GetTickCount() + kVisibleOverlayMs;

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
    UpdateOverlayPosition(overlay, gameWindow);
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

void ShowLaunchOverlayForProcess(uint32_t processId, LaunchOverlayInfo info)
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
        + std::to_string(info.resourceModCount);

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

int RunLaunchOverlayProcess(uint32_t processId, LaunchOverlayInfo info)
{
    return RunOverlayWindow(processId, std::move(info));
}
}
