#pragma once

#include <vector>
#include <windows.h>

namespace common {

inline wchar_t *wcsstrip(wchar_t *s)
{
    auto count = std::wcslen(s);
    if (count == 0) {
        return s;
    }

    auto end = s + count - 1;
    while (end >= s && std::isspace(*end)) {
        end--;
    }
    *(end + 1) = L'\0';

    while ((*s != L'\0') && std::isspace(*s)) {
        s++;
    }

    return s;
}

inline const wchar_t *parse_double(const wchar_t *s, double &value)
{
    wchar_t *end;
    value = std::wcstod(s, &end);
    if (end == s) {
        value = NAN;
    }

    while (*end != L'\0') {
        if (!std::isspace(*end)) {
            break;
        }
        ++end;
    }

    return end;
}

namespace win32 {

namespace {

using ZwSetTimerResolution_t = NTSTATUS (WINAPI *)(IN ULONG RequestedResolution, IN BOOLEAN Set, OUT PULONG ActualResolution);
auto ZwSetTimerResolution = reinterpret_cast<ZwSetTimerResolution_t>(GetProcAddress(LoadLibraryW(L"ntdll.dll"), "ZwSetTimerResolution"));

using NtDelayExecution_t = NTSTATUS (WINAPI *)(IN BOOL Alertable, IN PLARGE_INTEGER DelayInterval);
auto NtDelayExecution = reinterpret_cast<NtDelayExecution_t>(GetProcAddress(LoadLibraryW(L"ntdll.dll"), "NtDelayExecution"));

inline long long performance_counter_frequency_()
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
}

}

constexpr auto VIRTUAL_KEYS = 256;

inline void WritePrivateProfileIntW(LPCWSTR lpAppName, LPCWSTR lpKeyName, int nInt, LPCWSTR lpFileName)
{
    wchar_t buffer[128];
    std::swprintf(buffer, std::size(buffer), L"%d", nInt);
    WritePrivateProfileStringW(lpAppName, lpKeyName, buffer, lpFileName);
}

inline long long performance_counter_frequency()
{
    static auto value = performance_counter_frequency_();
    return value;
}

inline long long performance_counter()
{
    LARGE_INTEGER i;
    QueryPerformanceCounter(&i);
    return i.QuadPart;
}

inline void set_timer_resolution(unsigned long hns)
{
    ULONG actual;
    ZwSetTimerResolution(hns, true, &actual);
}

inline void delay_execution_by(long long hns)
{
    LARGE_INTEGER interval;
    interval.QuadPart = -1 * hns;
    NtDelayExecution(false, &interval);
}

inline void move_mouse_by(int x, int y)
{
    INPUT input;
    input.type = INPUT_MOUSE;
    input.mi.dx = x;
    input.mi.dy = y;
    input.mi.mouseData = 0;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    input.mi.time = 0;
    input.mi.dwExtraInfo = 0;
    SendInput(1, &input, sizeof(input));
}

inline bool is_key_down(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000);
}

inline bool get_vk_string(int vk, wchar_t *result, size_t count)
{
    UINT scan_code;
    switch (vk) {
    case VK_LBUTTON:
        std::swprintf(result, count, L"Left Mouse Button");
        return true;
    case VK_RBUTTON:
        std::swprintf(result, count, L"Right Mouse Button");
        return true;
    case VK_MBUTTON:
        std::swprintf(result, count, L"Middle Mouse Button");
        return true;
    case VK_XBUTTON1:
        std::swprintf(result, count, L"X1 Mouse Button");
        return true;
    case VK_XBUTTON2:
        std::swprintf(result, count, L"X2 Mouse Button");
        return true;
    case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN: case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME: case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
        scan_code = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) | 0x100;
        break;
    default:
        scan_code = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        break;
    }
    return GetKeyNameTextW(scan_code << 16, result, count);
}

namespace {

struct ConsoleBase
{
    HANDLE handle;

    ConsoleBase(HANDLE handle) : handle(handle) {}

    DWORD get_mode()
    {
        DWORD mode;
        GetConsoleMode(handle, &mode);
        return mode;
    }

    void set_mode(DWORD mode)
    {
        SetConsoleMode(handle, mode);
    }
};

}

struct ConsoleInput :
    ConsoleBase
{
    ConsoleInput(HANDLE handle) : ConsoleBase(handle) {}

    size_t consume_input_events(INPUT_RECORD *buffer, DWORD count)
    {
        DWORD available;
        GetNumberOfConsoleInputEvents(handle, &available);
        if (available == 0) {
            return 0;
        }
        count = std::min(available, count);
        ReadConsoleInputW(handle, buffer, count, &available);
        return count;
    }

    void write_text(const wchar_t *s)
    {
        auto count = std::wcslen(s);
        INPUT_RECORD records[count];
        for (int i = 0; i < count; ++i) {
            records[i].EventType = KEY_EVENT;
            records[i].Event.KeyEvent.bKeyDown = true;
            records[i].Event.KeyEvent.wRepeatCount = 1;
            records[i].Event.KeyEvent.wVirtualKeyCode = 0;
            records[i].Event.KeyEvent.wVirtualScanCode = 0;
            records[i].Event.KeyEvent.uChar.UnicodeChar = s[i];
            records[i].Event.KeyEvent.dwControlKeyState = 0;
        }

        DWORD written;
        WriteConsoleInput(handle, records, count, &written);
    }

    void read_text(wchar_t *buffer, size_t count)
    {
        DWORD read;
        ReadConsoleW(handle, buffer, count, &read, nullptr);

        if (buffer[read - 1] == L'\n') {
            buffer[read - 2] = L'\0';
            return;
        }

        buffer[read - 1] = L'\0';

        wchar_t rest[1024];
        do {
            ReadConsoleW(handle, rest, std::size(rest), &read, nullptr);
        } while (rest[read - 1] != L'\n');
    }
};

struct ConsoleOutput :
    ConsoleBase
{
    ConsoleOutput(HANDLE handle) : ConsoleBase(handle) {}

    void set_as_active_screen_buffer()
    {
        SetConsoleActiveScreenBuffer(handle);
    }

    CONSOLE_CURSOR_INFO get_cursor_info()
    {
        CONSOLE_CURSOR_INFO info;
        GetConsoleCursorInfo(handle, &info);
        return info;
    }

    void set_cursor_info(const CONSOLE_CURSOR_INFO &info)
    {
        SetConsoleCursorInfo(handle, &info);
    }

    CONSOLE_SCREEN_BUFFER_INFOEX get_screen_buffer_info()
    {
        CONSOLE_SCREEN_BUFFER_INFOEX info;
        info.cbSize = sizeof(info);
        GetConsoleScreenBufferInfoEx(handle, &info);
        return info;
    }

    void set_window_info(bool absolute, const SMALL_RECT &window)
    {
        SetConsoleWindowInfo(handle, absolute, &window);
    }

    void set_buffer_size(COORD size)
    {
        SetConsoleScreenBufferSize(handle, size);
    }

    void set_cursor_position(COORD position)
    {
        SetConsoleCursorPosition(handle, position);
    }

    void set_text_attributes(WORD attributes)
    {
        SetConsoleTextAttribute(handle, attributes);
    }

    void fill(wchar_t c, COORD position, size_t count)
    {
        DWORD written;
        FillConsoleOutputCharacterW(handle, c, count, position, &written);
    }

    void fill_attribute(WORD attribute, COORD position, size_t count)
    {
        DWORD written;
        FillConsoleOutputAttribute(handle, attribute, count, position, &written);
    }

    void write(const wchar_t *s, size_t count)
    {
        WriteConsoleW(handle, s, count, nullptr, nullptr);
    }

    void write_text(const wchar_t *s)
    {
        WriteConsoleW(handle, s, std::wcslen(s), nullptr, nullptr);
    }

    void write_info(const wchar_t *s, size_t count, CONSOLE_SCREEN_BUFFER_INFOEX &info)
    {
        count = std::min(count, size_t(info.dwSize.X - info.dwCursorPosition.X));
        WriteConsoleW(handle, s, count, nullptr, nullptr);
        info.dwCursorPosition.X += count;
    }

    void write_text_info(const wchar_t *s, CONSOLE_SCREEN_BUFFER_INFOEX &info)
    {
        size_t count = std::min(std::wcslen(s), size_t(info.dwSize.X - info.dwCursorPosition.X));
        WriteConsoleW(handle, s, count, nullptr, nullptr);
        info.dwCursorPosition.X += count;
    }
};

struct CursorVisibilityObserver
{
    CursorVisibilityObserver()
    {
        SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE, nullptr, proc, 0, 0, WINEVENT_OUTOFCONTEXT);
        SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, nullptr, proc, 0, 0, WINEVENT_OUTOFCONTEXT);

        visible_ = check_visible();
    }

    auto visible()
    {
        return visible_;
    }

private:
    static void proc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD idEventThread, DWORD dwmsEventTime)
    {
        if (idObject != OBJID_CURSOR) {
            return;
        }
        visible_ = check_visible();
    }

    static bool check_visible()
    {
        CURSORINFO info = {};
        info.cbSize = sizeof(info);
        GetCursorInfo(&info);
        if (!(info.flags & CURSOR_SHOWING)) {
            return false;
        }

        ICONINFOEXW icon_info = {};
        icon_info.cbSize = sizeof(ICONINFOEXW);
        GetIconInfoExW(info.hCursor, &icon_info);
        return icon_info.hbmColor != nullptr || *icon_info.szModName != L'\0';
    }

    static inline bool visible_;
};

struct CtrlSignalHandler
{
    CtrlSignalHandler(HWND quit_hwnd_)
    {
        quit_hwnd = quit_hwnd_;
        event = CreateEventW(nullptr, false, false, nullptr);

        SetConsoleCtrlHandler(proc, true);
    }

    void done()
    {
        SetEvent(event);
    }

private:
    static BOOL proc(DWORD dwCtrlType)
    {
        switch (dwCtrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            PostMessageW(quit_hwnd, WM_QUIT, 0, 0);
            WaitForSingleObject(event, INFINITE);
            return true;
        }
        return false;
    }

    static inline HWND quit_hwnd;
    static inline HANDLE event;
};

}

}
