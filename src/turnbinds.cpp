#include <cmath>
#include <optional>
#include <vector>
#include <utility>
#include <pathcch.h>
#include <windows.h>
#include <windns.h>
#include <hidusage.h>

namespace common {

wchar_t *wcsstrip(wchar_t *s)
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

const wchar_t *parse_double(const wchar_t *s, double &value)
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

long long performance_counter_frequency()
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
}

}

constexpr auto VIRTUAL_KEYS = 256;

const auto PERFORMANCE_COUNTER_FREQUENCY = performance_counter_frequency();

void WritePrivateProfileIntW(LPCWSTR lpAppName, LPCWSTR lpKeyName, int nInt, LPCWSTR lpFileName)
{
    wchar_t buffer[128];
    std::swprintf(buffer, std::size(buffer), L"%d", nInt);
    WritePrivateProfileStringW(lpAppName, lpKeyName, buffer, lpFileName);
}

long long performance_counter()
{
    LARGE_INTEGER i;
    QueryPerformanceCounter(&i);
    return i.QuadPart;
}

void set_timer_resolution(unsigned long hns)
{
    ULONG actual;
    ZwSetTimerResolution(hns, true, &actual);
}

void delay_execution_by(long long hns)
{
    LARGE_INTEGER interval;
    interval.QuadPart = -1 * hns;
    NtDelayExecution(false, &interval);
}

void move_mouse_by(int x, int y)
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

bool is_key_down(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000);
}

bool get_vk_string(int vk, wchar_t *result, size_t count)
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

size_t get_raw_input_msgs(const RAWINPUT &input, USHORT (&vks)[5], UINT (&msgs)[5])
{
    constexpr int MOUSE_DOWN[5] = {RI_MOUSE_BUTTON_1_DOWN, RI_MOUSE_BUTTON_2_DOWN, RI_MOUSE_BUTTON_3_DOWN, RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_5_DOWN};
    constexpr int MOUSE_UP[5] = {RI_MOUSE_BUTTON_1_UP, RI_MOUSE_BUTTON_2_UP, RI_MOUSE_BUTTON_3_UP, RI_MOUSE_BUTTON_4_UP, RI_MOUSE_BUTTON_5_UP};
    constexpr int MOUSE_VK[5] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};

    size_t count = 0;
    switch (input.header.dwType) {
    case RIM_TYPEMOUSE:
        for (int i = 0; i < std::size(MOUSE_DOWN); ++i) {
            if (input.data.mouse.usButtonFlags & MOUSE_DOWN[i]) {
                vks[count] = MOUSE_VK[i];
                msgs[count] = WM_KEYDOWN;
                ++count;
            } else if (input.data.mouse.usButtonFlags & MOUSE_UP[i]) {
                vks[count] = MOUSE_VK[i];
                msgs[count] = WM_KEYUP;
                ++count;
            }
        }
        break;
    case RIM_TYPEKEYBOARD:
        switch (input.data.keyboard.VKey) {
        case VK_CONTROL:
            if (input.header.hDevice == nullptr) {
                vks[count] = VK_ZOOM;
            } else {
                vks[count] = (input.data.keyboard.Flags & RI_KEY_E0 ? VK_RCONTROL : VK_LCONTROL);
            }
            break;
        case VK_MENU:
            vks[count] = (input.data.keyboard.Flags & RI_KEY_E0 ? VK_RMENU : VK_LMENU);
            break;
        case VK_SHIFT:
            vks[count] = (input.data.keyboard.MakeCode == 0x36 ? VK_RSHIFT : VK_LSHIFT);
            break;
        default:
            vks[count] = input.data.keyboard.VKey;
            break;
        }
        msgs[count] = input.data.keyboard.Message;
        ++count;
        break;
    }
    return count;
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

}

}

using namespace common;

enum class Bind
{
    LEFT,
    RIGHT,
    SPEED,
    CYCLE,
    COUNT
};

enum class Var
{
    RATE,
    SLEEP,
    COUNT
};

enum class CycleVar
{
    YAWSPEED,
    SENSITIVITY,
    ANGLESPEEDKEY,
    YAW,
    COUNT
};

enum class Switch
{
    ENABLED,
    RAW_INPUT,
    COUNT
};

enum class ConsoleItem
{
    BIND_LEFT,
    BIND_RIGHT,
    BIND_SPEED,
    BIND_CYCLE,
    CL_YAWSPEED,
    SENSITIVITY,
    CL_ANGLESPEEDKEY,
    M_YAW,
    ENABLED,
    RAW_INPUT,
    RATE,
    SLEEP,
    COUNT
};

enum class ConsoleItemType
{
    BIND,
    CYCLE_VAR,
    SWITCH,
    VAR,
    COUNT
};

static constexpr auto console_item_type(ConsoleItem item)
{
    switch (item) {
    case ConsoleItem::BIND_LEFT:
    case ConsoleItem::BIND_RIGHT:
    case ConsoleItem::BIND_SPEED:
    case ConsoleItem::BIND_CYCLE:
        return ConsoleItemType::BIND;
    case ConsoleItem::CL_YAWSPEED:
    case ConsoleItem::SENSITIVITY:
    case ConsoleItem::CL_ANGLESPEEDKEY:
    case ConsoleItem::M_YAW:
        return ConsoleItemType::CYCLE_VAR;
    case ConsoleItem::ENABLED:
    case ConsoleItem::RAW_INPUT:
        return ConsoleItemType::SWITCH;
    case ConsoleItem::RATE:
    case ConsoleItem::SLEEP:
        return ConsoleItemType::VAR;
    default:
        return ConsoleItemType::COUNT;
    }
}

static constexpr auto is_developer_console_item(ConsoleItem item)
{
    switch (item) {
    case ConsoleItem::RAW_INPUT:
    case ConsoleItem::RATE:
    case ConsoleItem::SLEEP:
        return true;
    default:
        return false;
    }
}

static constexpr auto is_line_break_console_item(ConsoleItem item)
{
    switch (item) {
    case ConsoleItem::CL_YAWSPEED:
    case ConsoleItem::ENABLED:
    case ConsoleItem::RAW_INPUT:
        return true;
    default:
        return false;
    }
}

static constexpr auto to_bind(ConsoleItem item)
{
    switch (item) {
    case ConsoleItem::BIND_LEFT:
        return Bind::LEFT;
    case ConsoleItem::BIND_RIGHT:
        return Bind::RIGHT;
    case ConsoleItem::BIND_SPEED:
        return Bind::SPEED;
    case ConsoleItem::BIND_CYCLE:
        return Bind::CYCLE;
    default:
        return Bind::COUNT;
    }
}

static constexpr auto to_var(ConsoleItem item)
{
    switch (item) {
    case ConsoleItem::RATE:
        return Var::RATE;
    case ConsoleItem::SLEEP:
        return Var::SLEEP;
    default:
        return Var::COUNT;
    }
}

static constexpr auto to_cycle_var(ConsoleItem item)
{
    switch (item) {
    case ConsoleItem::CL_YAWSPEED:
        return CycleVar::YAWSPEED;
    case ConsoleItem::SENSITIVITY:
        return CycleVar::SENSITIVITY;
    case ConsoleItem::CL_ANGLESPEEDKEY:
        return CycleVar::ANGLESPEEDKEY;
    case ConsoleItem::M_YAW:
        return CycleVar::YAW;
    default:
        return CycleVar::COUNT;
    }
}

static constexpr auto to_switch(ConsoleItem item)
{
    switch (item) {
    case ConsoleItem::ENABLED:
        return Switch::ENABLED;
    case ConsoleItem::RAW_INPUT:
        return Switch::RAW_INPUT;
    default:
        return Switch::COUNT;
    }
}

struct State
{
    bool developer;

    UINT binds[std::to_underlying(Bind::COUNT)];
    std::optional<double> vars[std::to_underlying(Var::COUNT)];
    std::vector<double> cycle_vars[std::to_underlying(CycleVar::COUNT)];
    bool switches[std::to_underlying(Switch::COUNT)];
    int current;

    bool valid;
    int count;

    wchar_t vars_text[std::to_underlying(Var::COUNT)][128];
    wchar_t cycle_vars_text[std::to_underlying(CycleVar::COUNT)][128];
    ConsoleItem selected;

    std::optional<WINDOWPLACEMENT> placement;

    void on_settings_loaded();
    void on_settings_save();
    void on_developer_changed();
    void on_var_changed(Var var);
    void on_cycle_vars_changed();
    void on_switch_changed(Switch switch_);
    void on_current_changed();
    void on_valid_updated();
    void on_selected_changed(ConsoleItem prev);

    void update_valid();
    void parse_var(Var var);
    void parse_cycle_vars();
} g_state;

struct Input
{
    std::array<bool, win32::VIRTUAL_KEYS> down;
    bool capturing = false;
    USHORT captured;
    bool binds = false;

    Input(HWND hwnd) : hwnd(hwnd) {}

    void start_capturing()
    {
        capturing = true;

        if (g_state.switches[std::to_underlying(Switch::RAW_INPUT)]) {
            if (++raw_input == 1) {
                enable_raw_input();
            }
        } else {
            for (int i = 0; i < std::size(down); ++i) {
                down[i] = win32::is_key_down(i);
            }
        }
    }

    void enable_binds()
    {
        binds = true;

        if (g_state.switches[std::to_underlying(Switch::RAW_INPUT)]) {
            if (++raw_input == 1) {
                enable_raw_input();
            }
        }

        for (int i = 0; i < std::size(g_state.binds); ++i) {
            down[g_state.binds[i]] = win32::is_key_down(g_state.binds[i]);
        }
    }

    void disable_binds()
    {
        binds = false;

        if (g_state.switches[std::to_underlying(Switch::RAW_INPUT)]) {
            if (--raw_input == 0) {
                disable_raw_input();
            }
        }
    }

    void on_switch_changed(Switch switch_)
    {
        switch (switch_) {
        case Switch::RAW_INPUT:
            if (!g_state.switches[std::to_underlying(switch_)] && raw_input > 0) {
                raw_input = 0;
                disable_raw_input();
            }
            if (capturing) {
                start_capturing();
            }
            if (binds) {
                enable_binds();
            }
            break;
        }
    }

    void run()
    {
        if (!g_state.switches[std::to_underlying(Switch::RAW_INPUT)]) {
            if (capturing) {
                for (int i = 0; i < std::size(down); ++i) {
                    auto last = down[i];
                    down[i] = win32::is_key_down(i);
                    if (!last && down[i]) {
                        if (i == VK_ESCAPE || i == VK_RETURN) {
                            continue;
                        }

                        auto scan = MapVirtualKeyW(i, MAPVK_VK_TO_VSC_EX);
                        if (scan != 0 && i != MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX)) {
                            continue;
                        }

                        capturing = false;
                        captured = i;
                        break;
                    }
                }
            }

            if (binds) {
                for (int i = 0; i < std::size(g_state.binds); ++i) {
                    down[g_state.binds[i]] = win32::is_key_down(g_state.binds[i]);
                }
            }
        }

        UINT size;
        GetRawInputBuffer(nullptr, &size, sizeof(RAWINPUTHEADER));
        size *= 1024;

        char buffer_[size];
        auto buffer = reinterpret_cast<RAWINPUT *>(buffer_);

        do {
            int read = GetRawInputBuffer(buffer, &size, sizeof(RAWINPUTHEADER));
            if (read == 0) {
                break;
            }

            if (!g_state.switches[std::to_underlying(Switch::RAW_INPUT)]) {
                continue;
            }

            for (const RAWINPUT *input = buffer; read-- > 0; input = NEXTRAWINPUTBLOCK(input)) {
                USHORT vks[5];
                UINT msgs[5];
                auto count = win32::get_raw_input_msgs(*input, vks, msgs);

                if (binds) {
                    for (int i = 0; i < count; ++i) {
                        for (int j = 0; j < std::size(g_state.binds); ++j) {
                            if (g_state.binds[j] == vks[i]) {
                                switch (msgs[i]) {
                                case WM_KEYDOWN:
                                case WM_SYSKEYDOWN:
                                    down[g_state.binds[j]] = true;
                                    break;
                                case WM_KEYUP:
                                case WM_SYSKEYUP:
                                    down[g_state.binds[j]] = false;
                                    break;
                                }
                            }
                        }
                    }
                }

                if (capturing) {
                    for (int i = 0; i < count; ++i) {
                        if (msgs[i] == WM_KEYDOWN) {
                            if (vks[i] == VK_ESCAPE || vks[i] == VK_RETURN) {
                                continue;
                            }

                            capturing = false;
                            captured = vks[i];

                            --raw_input;
                            disable_raw_input();
                            break;
                        }
                    }
                }
            }
        } while (true);
    }

private:
    HWND hwnd;
    int raw_input = 0;

    void enable_raw_input()
    {
        RAWINPUTDEVICE devices[2];
        devices[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
        devices[0].usUsage = HID_USAGE_GENERIC_MOUSE;
        devices[0].dwFlags = RIDEV_INPUTSINK | RIDEV_NOLEGACY;
        devices[0].hwndTarget = hwnd;
        devices[1].usUsagePage = HID_USAGE_PAGE_GENERIC;
        devices[1].usUsage = HID_USAGE_GENERIC_KEYBOARD;
        devices[1].dwFlags = RIDEV_INPUTSINK | RIDEV_NOLEGACY;
        devices[1].hwndTarget = hwnd;
        RegisterRawInputDevices(devices, std::size(devices), sizeof(*devices));
    }

    void disable_raw_input()
    {
        RAWINPUTDEVICE devices[2];
        devices[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
        devices[0].usUsage = HID_USAGE_GENERIC_MOUSE;
        devices[0].dwFlags = RIDEV_REMOVE;
        devices[0].hwndTarget = nullptr;
        devices[1].usUsagePage = HID_USAGE_PAGE_GENERIC;
        devices[1].usUsage = HID_USAGE_GENERIC_KEYBOARD;
        devices[1].dwFlags = RIDEV_REMOVE;
        devices[1].hwndTarget = nullptr;
        RegisterRawInputDevices(devices, std::size(devices), sizeof(*devices));
    }
};

struct MouseMoveCalculator
{
    long long update(bool reset, Input &input)
    {
        if (reset) {
            last_down = {};
        }

        if (!last_down[std::to_underlying(Bind::CYCLE)] && input.down[g_state.binds[std::to_underlying(Bind::CYCLE)]]) {
            g_state.current = (g_state.current + 1) % g_state.count;
            g_state.on_current_changed();
        }

        auto time = win32::performance_counter();

        if ((last_down[std::to_underlying(Bind::LEFT)] ^ input.down[g_state.binds[std::to_underlying(Bind::LEFT)]]) || (last_down[std::to_underlying(Bind::RIGHT)] ^ input.down[g_state.binds[std::to_underlying(Bind::RIGHT)]])) {
            last_time = time;
            remaining = 0.0;
        }

        for (int i = 0; i < std::size(last_down); ++i) {
            last_down[i] = input.down[g_state.binds[i]];
        }

        if (!(input.down[g_state.binds[std::to_underlying(Bind::LEFT)]] ^ input.down[g_state.binds[std::to_underlying(Bind::RIGHT)]]) || time - last_time < win32::PERFORMANCE_COUNTER_FREQUENCY * (1.0 / *g_state.vars[std::to_underlying(Var::RATE)])) {
            return 0;
        }

        double cycle_vars[std::to_underlying(CycleVar::COUNT)];
        for (int i = 0; i < std::size(cycle_vars); ++i) {
            cycle_vars[i] = g_state.cycle_vars[i][g_state.current % g_state.cycle_vars[i].size()];
        }

        remaining +=
            (
                (int(input.down[g_state.binds[std::to_underlying(Bind::LEFT)]]) * -1 + int(input.down[g_state.binds[std::to_underlying(Bind::RIGHT)]])) *
                (cycle_vars[std::to_underlying(CycleVar::YAWSPEED)] / (cycle_vars[std::to_underlying(CycleVar::SENSITIVITY)] * cycle_vars[std::to_underlying(CycleVar::YAW)])) *
                (input.down[g_state.binds[std::to_underlying(Bind::SPEED)]] ? cycle_vars[std::to_underlying(CycleVar::ANGLESPEEDKEY)] : 1.0) *
                (time - last_time)
            ) / win32::PERFORMANCE_COUNTER_FREQUENCY;

        auto amount = static_cast<long long>(remaining);
        remaining -= amount;
        last_time = time;
        return amount;
    }

private:
    long long last_time;
    double remaining;
    std::array<bool, std::to_underlying(Bind::COUNT)> last_down;
};

struct VersionInfo
{
    const wchar_t *name;
    const wchar_t *title;
    const wchar_t *version;
    const wchar_t *copyright;

    VersionInfo(const wchar_t *path)
    {
        DWORD handle;
        auto size = GetFileVersionInfoSizeW(path, &handle);

        data.resize(size);
        GetFileVersionInfoW(path, handle, data.size(), data.data());

        UINT count;
        VerQueryValueW(data.data(), L"\\StringFileInfo\\040904E4\\InternalName", reinterpret_cast<void **>(const_cast<wchar_t **>(&name)), &count);
        VerQueryValueW(data.data(), L"\\StringFileInfo\\040904E4\\ProductName", reinterpret_cast<void **>(const_cast<wchar_t **>(&title)), &count);
        VerQueryValueW(data.data(), L"\\StringFileInfo\\040904E4\\ProductVersion", reinterpret_cast<void **>(const_cast<wchar_t **>(&version)), &count);
        VerQueryValueW(data.data(), L"\\StringFileInfo\\040904E4\\LegalCopyright", reinterpret_cast<void **>(const_cast<wchar_t **>(&copyright)), &count);
    }

private:
    std::vector<char> data;
};

struct ConsoleWindow
{
    ConsoleWindow(const VersionInfo &version_info, HICON icon) :
        version_info(version_info),
        hwnd(GetConsoleWindow())
    {
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        SendMessage(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));

        SetWindowLong(hwnd, GWL_STYLE, GetWindowLong(hwnd, GWL_STYLE) & ~WS_SIZEBOX & ~WS_MAXIMIZEBOX);
    }

    void on_settings_loaded()
    {
        if (g_state.placement) {
            SetWindowPlacement(hwnd, &(*g_state.placement));
        }
        reset_title();
        ShowWindow(hwnd, SW_SHOWNORMAL);
    }

    void on_settings_save()
    {
        g_state.placement = {};
        GetWindowPlacement(hwnd, &(*g_state.placement));
    }

    void on_switch_changed(Switch switch_)
    {
        switch (switch_) {
        case Switch::ENABLED:
            reset_title();
            break;
        }
    }

    void on_valid_updated()
    {
        reset_title();
    }

private:
    const VersionInfo &version_info;

    HWND hwnd;

    void reset_title()
    {
        wchar_t buffer[128];
        std::swprintf(buffer, std::size(buffer), L"%s%s", version_info.title, (!g_state.valid ? L" (error)" : (g_state.switches[std::to_underlying(Switch::ENABLED)] ? L"" : L" (disabled)")));
        SetConsoleTitleW(buffer);
    }
};

struct Console
{
    win32::ConsoleOutput out;
    win32::ConsoleInput in;
    bool editing = false;

    Console(const VersionInfo &version_info) :
        out(CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, nullptr, CONSOLE_TEXTMODE_BUFFER, nullptr)),
        in(GetStdHandle(STD_INPUT_HANDLE)),
        version_info(version_info)
    {
        initial_in_mode = in.get_mode();

        out.set_mode(ENABLE_PROCESSED_OUTPUT);
        out.set_as_active_screen_buffer();

        in.set_mode(ENABLE_PROCESSED_INPUT | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);
    }

    ~Console()
    {
        in.set_mode(initial_in_mode);
    }

    void on_settings_loaded()
    {
        resize();
    }

    void on_developer_changed()
    {
        resize();
    }

    void on_switch_changed(Switch switch_)
    {
        switch (switch_) {
        case Switch::ENABLED:
            redraw_item_value(ConsoleItem::ENABLED);
            break;
        case Switch::RAW_INPUT:
            redraw_item_value(ConsoleItem::RAW_INPUT);
            break;
        }
    }

    void on_current_changed()
    {
        redraw_item_value(ConsoleItem::CL_YAWSPEED);
        redraw_item_value(ConsoleItem::SENSITIVITY);
        redraw_item_value(ConsoleItem::CL_ANGLESPEEDKEY);
        redraw_item_value(ConsoleItem::M_YAW);
    }

    void on_selected_changed(ConsoleItem prev)
    {
        redraw_selector(prev);
    }

    void run(HWND hwnd, Input &input)
    {
        do {
            INPUT_RECORD events[1024];
            auto count = in.consume_input_events(events, std::size(events));
            if (count == 0) {
                break;
            }

            if (editing) {
                continue;
            }

            for (int i = 0; i < count; ++i) {
                switch (events[i].EventType) {
                case WINDOW_BUFFER_SIZE_EVENT:
                    redraw_full();
                    break;
                case KEY_EVENT:
                    switch (events[i].Event.KeyEvent.wVirtualKeyCode) {
                    case VK_ESCAPE:
                        if (events[i].Event.KeyEvent.bKeyDown) {
                            PostMessageW(hwnd, WM_QUIT, 0, 0);
                        }
                        break;
                    case VK_UP:
                    case VK_DOWN:
                        if (events[i].Event.KeyEvent.bKeyDown) {
                            bool down = (events[i].Event.KeyEvent.wVirtualKeyCode == VK_DOWN);
                            auto prev = g_state.selected;
                            do {
                                g_state.selected = static_cast<ConsoleItem>((down ? (std::to_underlying(g_state.selected) + 1) : (std::to_underlying(g_state.selected) - 1 + std::to_underlying(ConsoleItem::COUNT) + 1)) % (std::to_underlying(ConsoleItem::COUNT) + 1));
                            } while (!g_state.developer && is_developer_console_item(g_state.selected));
                            g_state.on_selected_changed(prev);
                        }
                        break;
                    case VK_LEFT:
                    case VK_RIGHT:
                        if (events[i].Event.KeyEvent.bKeyDown) {
                            bool right = (events[i].Event.KeyEvent.wVirtualKeyCode == VK_RIGHT);
                            switch (console_item_type(g_state.selected)) {
                            case ConsoleItemType::CYCLE_VAR:
                            case ConsoleItemType::COUNT:
                                if (g_state.count > 0) {
                                    g_state.current = (right ? (g_state.current + 1) : (g_state.current - 1 + g_state.count)) % g_state.count;
                                    g_state.on_current_changed();
                                }
                                break;
                            case ConsoleItemType::SWITCH:
                                {
                                    auto switch_ = to_switch(g_state.selected);
                                    g_state.switches[std::to_underlying(switch_)] = right;
                                    g_state.on_switch_changed(switch_);
                                }
                                break;
                            }
                        }
                        break;
                    case VK_RETURN:
                        if (events[i].Event.KeyEvent.bKeyDown) {
                            switch (console_item_type(g_state.selected)) {
                            case ConsoleItemType::BIND:
                                editing = true;
                                redraw_item_value(g_state.selected);
                                input.start_capturing();

                                count = 0;
                                break;
                            case ConsoleItemType::VAR:
                                editing = true;

                                read_input_value(g_state.selected);

                                editing = false;

                                g_state.on_var_changed(to_var(g_state.selected));
                                redraw_full();
                                break;
                            case ConsoleItemType::CYCLE_VAR:
                                editing = true;

                                for (int i = 0; i < std::to_underlying(ConsoleItem::COUNT); ++i) {
                                    switch (console_item_type(static_cast<ConsoleItem>(i))) {
                                    case ConsoleItemType::CYCLE_VAR:
                                        redraw_item_value(static_cast<ConsoleItem>(i));
                                        break;
                                    }
                                }

                                read_input_value(g_state.selected);

                                editing = false;

                                g_state.on_cycle_vars_changed();
                                redraw_full();
                                break;
                            case ConsoleItemType::COUNT:
                                g_state.developer = !g_state.developer;
                                g_state.on_developer_changed();
                                break;
                            }
                        }
                        break;
                    }
                    break;
                }
            }
        } while (true);

        if (editing) {
            switch (console_item_type(g_state.selected)) {
            case ConsoleItemType::BIND:
                if (input.capturing) {
                    break;
                }

                g_state.binds[std::to_underlying(to_bind(g_state.selected))] = input.captured;
                editing = false;

                redraw_item_value(g_state.selected);
                break;
            }
        }
    }

private:
    static constexpr const wchar_t *CONSOLE_ITEM_NAMES[] = {
        L"+left",
        L"+right",
        L"+speed",
        L"cycle",
        L"cl_yawspeed",
        L"sensitivity",
        L"cl_anglespeedkey",
        L"m_yaw",
        L"enabled",
        L"raw input",
        L"rate",
        L"sleep"
    };
    static constexpr const wchar_t *SELECTOR[] = {L"   ", L" » "};
    static constexpr SHORT BUFFER_WIDTH = 64;
    static constexpr SHORT BUFFER_HEIGHT[] = {17, 21};
    static constexpr auto INPUT_PADDING = 18;

    const VersionInfo &version_info;

    DWORD initial_in_mode;

    COORD positions_selector[std::to_underlying(ConsoleItem::COUNT)];
    COORD positions_value[std::to_underlying(ConsoleItem::COUNT)];

    void resize()
    {
        out.set_window_info(true, {0, 0, 0, 0});
        out.set_buffer_size({1, 1});
        out.set_window_info(true, {0, 0, BUFFER_WIDTH - 1, static_cast<SHORT>(BUFFER_HEIGHT[g_state.developer] - 1)});
        out.set_buffer_size({BUFFER_WIDTH, static_cast<SHORT>(BUFFER_HEIGHT[g_state.developer])});
        out.set_window_info(true, {0, 0, BUFFER_WIDTH - 1, static_cast<SHORT>(BUFFER_HEIGHT[g_state.developer] - 1)});
    }

    void read_input_value(ConsoleItem item)
    {
        auto prev_out_mode = out.get_mode();
        auto prev_in_mode = in.get_mode();
        auto prev_cursor_info = out.get_cursor_info();

        out.set_mode(ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT);
        in.set_mode(ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_INSERT_MODE | ENABLE_EXTENDED_FLAGS);

        {
            auto info = prev_cursor_info;
            info.bVisible = true;
            out.set_cursor_info(info);
        }

        out.set_cursor_position(positions_value[std::to_underlying(item)]);

        wchar_t *text;
        size_t count;
        switch (console_item_type(item)) {
        case ConsoleItemType::VAR:
            text = g_state.vars_text[std::to_underlying(to_var(item))];
            count = std::size(g_state.vars_text[0]);
            break;
        case ConsoleItemType::CYCLE_VAR:
            text = g_state.cycle_vars_text[std::to_underlying(to_cycle_var(item))];
            count = std::size(g_state.cycle_vars_text[0]);
            break;
        }

        in.write_text(text);

        wchar_t buffer[count];
        in.read_text(buffer, count);
        std::wcscpy(text, wcsstrip(buffer));

        out.set_cursor_info(prev_cursor_info);
        in.set_mode(prev_in_mode);
        out.set_mode(prev_out_mode);

        out.set_cursor_position(positions_value[std::to_underlying(item)]);
    }

    void redraw_full()
    {
        auto info = out.get_screen_buffer_info();
        auto initial_cursor = info.dwCursorPosition;

        size_t count = info.dwSize.X + 1;
        wchar_t buffer[count];

        {
            auto info = out.get_cursor_info();
            info.bVisible = false;
            out.set_cursor_info(info);
        }

        out.fill_attribute(0, {0, 0}, info.dwSize.X * info.dwSize.Y);

        out.set_cursor_position(info.dwCursorPosition = {0, 0});
        out.fill(L' ', info.dwCursorPosition, info.dwSize.X - info.dwCursorPosition.X);

        for (int i = 0; i < std::to_underlying(ConsoleItem::COUNT); ++i) {
            if (!g_state.developer && is_developer_console_item(static_cast<ConsoleItem>(i))) {
                continue;
            }

            auto type = console_item_type(static_cast<ConsoleItem>(i));

            out.set_cursor_position(info.dwCursorPosition = {0, static_cast<SHORT>(info.dwCursorPosition.Y + 1)});

            if (is_line_break_console_item(static_cast<ConsoleItem>(i))) {
                out.fill(L' ', info.dwCursorPosition, info.dwSize.X - info.dwCursorPosition.X);
                out.set_cursor_position(info.dwCursorPosition = {0, static_cast<SHORT>(info.dwCursorPosition.Y + 1)});
            }

            positions_selector[i] = info.dwCursorPosition;
            out.write_text_info(SELECTOR[g_state.selected == static_cast<ConsoleItem>(i)], info);

            bool valid = true;
            switch (type) {
            case ConsoleItemType::VAR:
                valid = (g_state.vars[std::to_underlying(to_var(static_cast<ConsoleItem>(i)))] != std::nullopt);
                break;
            case ConsoleItemType::CYCLE_VAR:
                valid = (g_state.count > 0);
                break;
            }
            if (!valid) {
                out.set_text_attributes(FOREGROUND_INTENSITY | FOREGROUND_RED);
            }
            std::swprintf(buffer, count, L"%-*s", INPUT_PADDING, CONSOLE_ITEM_NAMES[i]);
            out.write_text_info(buffer, info);
            if (!valid) {
                out.set_text_attributes(info.wAttributes);
            }

            out.write_text_info(L": ", info);

            positions_value[i] = info.dwCursorPosition;
            draw_item_value(static_cast<ConsoleItem>(i), info);
            out.fill(L' ', info.dwCursorPosition, info.dwSize.X - info.dwCursorPosition.X);
        }

        out.set_cursor_position(info.dwCursorPosition = {0, static_cast<SHORT>(info.dwCursorPosition.Y + 1)});
        out.fill(L' ', info.dwCursorPosition, info.dwSize.X - info.dwCursorPosition.X);

        out.set_cursor_position(info.dwCursorPosition = {0, static_cast<SHORT>(info.dwCursorPosition.Y + 1)});
        out.fill(L' ', info.dwCursorPosition, info.dwSize.X - info.dwCursorPosition.X);

        {
            out.set_text_attributes(FOREGROUND_RED | FOREGROUND_GREEN);

            wchar_t title[BUFFER_WIDTH + 1];
            std::swprintf(title, std::size(title), L"%s %s", version_info.title, version_info.version);
            std::swprintf(buffer, count, L"%*s", info.dwSize.X - 2, title);

            out.set_cursor_position(info.dwCursorPosition = {0, static_cast<SHORT>(info.dwCursorPosition.Y + 1)});
            out.write_text_info(buffer, info);
            out.fill(L' ', info.dwCursorPosition, info.dwSize.X - info.dwCursorPosition.X);

            std::swprintf(buffer, count, L"%*s", info.dwSize.X - 2, version_info.copyright);

            out.set_cursor_position(info.dwCursorPosition = {0, static_cast<SHORT>(info.dwCursorPosition.Y + 1)});
            out.write_text_info(buffer, info);

            out.set_text_attributes(info.wAttributes);
        }

        out.fill(L' ', info.dwCursorPosition, info.dwSize.X * info.dwSize.Y);

        out.set_cursor_position(initial_cursor);
    }

    void redraw_item_value(ConsoleItem item)
    {
        out.set_cursor_position(positions_value[std::to_underlying(item)]);
        auto info = out.get_screen_buffer_info();
        draw_item_value(item, info);
        out.fill(L' ', info.dwCursorPosition, info.dwSize.X - info.dwCursorPosition.X);
    }

    void redraw_selector(ConsoleItem prev)
    {
        if (prev != ConsoleItem::COUNT) {
            out.set_cursor_position(positions_selector[std::to_underlying(prev)]);
            out.write_text(SELECTOR[false]);
        }

        if (g_state.selected != ConsoleItem::COUNT) {
            out.set_cursor_position(positions_selector[std::to_underlying(g_state.selected)]);
            out.write_text(SELECTOR[true]);
        }
    }

    void draw_item_value(ConsoleItem item, CONSOLE_SCREEN_BUFFER_INFOEX &info)
    {
        switch (console_item_type(item)) {
        case ConsoleItemType::BIND:
            {
                if (editing) {
                    out.write_text_info(L"<press any key>", info);
                    break;
                }

                auto bind = to_bind(item);

                wchar_t buffer[BUFFER_WIDTH + 1];
                if (!win32::get_vk_string(g_state.binds[std::to_underlying(bind)], buffer, std::size(buffer))) {
                    buffer[0] = L'\0';
                }

                out.write_text_info(buffer, info);
            }
            break;
        case ConsoleItemType::VAR:
            {
                if (editing && g_state.selected == item) {
                    break;
                }

                auto var = to_var(item);
                out.write_text_info(g_state.vars_text[std::to_underlying(var)], info);
            }
            break;
        case ConsoleItemType::CYCLE_VAR:
            {
                if (editing && g_state.selected == item) {
                    break;
                }

                auto cycle_var = to_cycle_var(item);

                if (editing || g_state.count == 0) {
                    out.write_text_info(g_state.cycle_vars_text[std::to_underlying(cycle_var)], info);
                    break;
                }

                const wchar_t *start = g_state.cycle_vars_text[std::to_underlying(cycle_var)];
                for (int i = 0; i < g_state.cycle_vars[std::to_underlying(cycle_var)].size(); ++i) {
                    auto *end = parse_double(start, g_state.cycle_vars[std::to_underlying(cycle_var)][i]);
                    auto current_end = end;
                    while (std::isspace(*(current_end - 1))) {
                        --current_end;
                    }

                    auto current = (g_state.cycle_vars[std::to_underlying(cycle_var)].size() != 1) && (i == g_state.current);

                    if (current) {
                        out.set_text_attributes(BACKGROUND_RED | BACKGROUND_BLUE | FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                    }
                    out.write_info(start, current_end - start, info);
                    if (current) {
                        out.set_text_attributes(info.wAttributes);
                    }
                    out.write_info(current_end, end - current_end, info);

                    start = end;
                }
            }
            break;
        case ConsoleItemType::SWITCH:
            {
                auto switch_ = to_switch(item);

                if (!g_state.switches[std::to_underlying(switch_)]) {
                    out.set_text_attributes(BACKGROUND_RED | BACKGROUND_BLUE | FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                }
                out.write_text_info(L"off", info);
                if (!g_state.switches[std::to_underlying(switch_)]) {
                    out.set_text_attributes(info.wAttributes);
                }

                out.write_text_info(L" ", info);

                if (g_state.switches[std::to_underlying(switch_)]) {
                    out.set_text_attributes(BACKGROUND_RED | BACKGROUND_BLUE | FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                }
                out.write_text_info(L"on", info);
                if (g_state.switches[std::to_underlying(switch_)]) {
                    out.set_text_attributes(info.wAttributes);
                }
            }
            break;
        }
    }
};

struct CursorMonitor
{
    CursorMonitor()
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
    CtrlSignalHandler(HWND hwnd_)
    {
        hwnd = hwnd_;
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
            PostMessageW(hwnd, WM_QUIT, 0, 0);
            WaitForSingleObject(event, INFINITE);
            return true;
        }
        return false;
    }

    static inline HWND hwnd;
    static inline HANDLE event;
};

struct App
{
    inline static std::optional<Input> input;
    inline static std::optional<Console> console;
    inline static std::optional<ConsoleWindow> console_window;

    static void run()
    {
        auto instance = GetModuleHandle(nullptr);

        wchar_t image_path[PATHCCH_MAX_CCH];
        GetModuleFileNameW(nullptr, image_path, std::size(image_path));

        if (std::getenv("__TURNBINDS_CONHOST") == nullptr) {
            SetEnvironmentVariableW(L"__TURNBINDS_CONHOST", L"");

            wchar_t command_line[MAX_PATH + 1 + std::size(image_path)];
            std::swprintf(command_line, std::size(command_line), L"conhost.exe %s", image_path);

            STARTUPINFOW startup_info = {};
            startup_info.cb = sizeof(STARTUPINFOW);
            startup_info.dwFlags = STARTF_USESHOWWINDOW;
            startup_info.wShowWindow = SW_HIDE;

            PROCESS_INFORMATION info;
            CreateProcessW(nullptr, command_line, nullptr, nullptr, true, 0, nullptr, nullptr, &startup_info, &info);

            return;
        }

        VersionInfo version_info(image_path);
        auto icon = static_cast<HICON>(::LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR));

        wchar_t ini_path[PATHCCH_MAX_CCH];
        std::wcscpy(ini_path, image_path);
        PathCchRenameExtension(ini_path, std::size(ini_path), L"ini");

        WNDCLASSEXW cls = {};
        cls.cbSize = sizeof(WNDCLASSEX);
        cls.lpfnWndProc = [](HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT {
            switch (uMsg) {
            case WM_CLOSE:
                DestroyWindow(hwnd);
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            default:
                return DefWindowProc(hwnd, uMsg, wParam, lParam);
            }
            return 0;
        };
        cls.hInstance = instance;
        cls.lpszClassName = version_info.name;
        RegisterClassExW(&cls);

        auto hwnd = CreateWindowExW(0, version_info.name, version_info.name, 0, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);

        input.emplace(hwnd);

        console.emplace(version_info);
        console_window.emplace(version_info, icon);

        MouseMoveCalculator mouse_move_calculator;
        CursorMonitor cursor_monitor;
        CtrlSignalHandler ctrl_signal_handler(hwnd);

        ini_load_settings(ini_path, version_info.name);
        g_state.on_settings_loaded();

        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        win32::set_timer_resolution(1);

        bool active = false;

        do {
            input->run();

        peek:
            MSG msg;
            if (PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    break;
                }
                DispatchMessage(&msg);
                goto peek;
            }

            console->run(hwnd, *input);

            auto last_active = active;
            active =
                !console->editing &&
                g_state.switches[std::to_underlying(Switch::ENABLED)] &&
                g_state.valid &&
                !cursor_monitor.visible();

            if (!active) {
                if (last_active) {
                    input->disable_binds();
                }

                HANDLE handles[] = {console->in.handle};
                MsgWaitForMultipleObjects(std::size(handles), handles, false, INFINITE, QS_ALLINPUT);
                continue;
            }

            if (!last_active) {
                input->enable_binds();
            }

            auto amount = mouse_move_calculator.update(!last_active, *input);
            if (amount != 0) {
                win32::move_mouse_by(amount, 0);
            }

            win32::delay_execution_by(*g_state.vars[std::to_underlying(Var::SLEEP)]);
        } while (true);

        console.reset();

        g_state.on_settings_save();
        ini_save_settings(ini_path, version_info.name);

        ctrl_signal_handler.done();
    }

    static void ini_load_settings(const wchar_t *path, const wchar_t *section)
    {
        g_state.developer = (GetPrivateProfileIntW(section, L"developer", 0, path) == 1);
        g_state.binds[std::to_underlying(Bind::LEFT)] = GetPrivateProfileIntW(section, L"bind_left", VK_LBUTTON, path);
        g_state.binds[std::to_underlying(Bind::RIGHT)] = GetPrivateProfileIntW(section, L"bind_right", VK_RBUTTON, path);
        g_state.binds[std::to_underlying(Bind::SPEED)] = GetPrivateProfileIntW(section, L"bind_speed", VK_LSHIFT, path);
        g_state.binds[std::to_underlying(Bind::CYCLE)] = GetPrivateProfileIntW(section, L"bind_cycle", VK_XBUTTON1, path);
        GetPrivateProfileStringW(section, L"rate", L"1000", g_state.vars_text[std::to_underlying(Var::RATE)], std::size(g_state.vars_text[std::to_underlying(Var::RATE)]), path);
        GetPrivateProfileStringW(section, L"sleep", L"3500", g_state.vars_text[std::to_underlying(Var::SLEEP)], std::size(g_state.vars_text[std::to_underlying(Var::SLEEP)]), path);
        GetPrivateProfileStringW(section, L"cl_yawspeed", L"75 120 210", g_state.cycle_vars_text[std::to_underlying(CycleVar::YAWSPEED)], std::size(g_state.cycle_vars_text[std::to_underlying(CycleVar::YAWSPEED)]), path);
        GetPrivateProfileStringW(section, L"sensitivity", L"1.0", g_state.cycle_vars_text[std::to_underlying(CycleVar::SENSITIVITY)], std::size(g_state.cycle_vars_text[std::to_underlying(CycleVar::SENSITIVITY)]), path);
        GetPrivateProfileStringW(section, L"cl_anglespeedkey", L"0.67", g_state.cycle_vars_text[std::to_underlying(CycleVar::ANGLESPEEDKEY)], std::size(g_state.cycle_vars_text[std::to_underlying(CycleVar::ANGLESPEEDKEY)]), path);
        GetPrivateProfileStringW(section, L"m_yaw", L"0.022", g_state.cycle_vars_text[std::to_underlying(CycleVar::YAW)], std::size(g_state.cycle_vars_text[std::to_underlying(CycleVar::YAW)]), path);
        g_state.switches[std::to_underlying(Switch::ENABLED)] = (GetPrivateProfileIntW(section, L"enabled", 1, path) == 1);
        g_state.switches[std::to_underlying(Switch::RAW_INPUT)] = (GetPrivateProfileIntW(section, L"raw_input", 0, path) == 1);
        g_state.current = GetPrivateProfileIntW(section, L"current", 0, path);
        g_state.selected = static_cast<ConsoleItem>(GetPrivateProfileIntW(section, L"selected", 0, path));

        WINDOWPLACEMENT placement;
        if (GetPrivateProfileStructW(section, L"placement", &placement, sizeof(placement), path)) {
            g_state.placement = placement;
        }
    }

    static void ini_save_settings(const wchar_t *path, const wchar_t *section)
    {
        win32::WritePrivateProfileIntW(section, L"developer", g_state.developer, path);
        win32::WritePrivateProfileIntW(section, L"bind_left", g_state.binds[std::to_underlying(Bind::LEFT)], path);
        win32::WritePrivateProfileIntW(section, L"bind_right", g_state.binds[std::to_underlying(Bind::RIGHT)], path);
        win32::WritePrivateProfileIntW(section, L"bind_speed", g_state.binds[std::to_underlying(Bind::SPEED)], path);
        win32::WritePrivateProfileIntW(section, L"bind_cycle", g_state.binds[std::to_underlying(Bind::CYCLE)], path);
        WritePrivateProfileStringW(section, L"rate", g_state.vars_text[std::to_underlying(Var::RATE)], path);
        WritePrivateProfileStringW(section, L"sleep", g_state.vars_text[std::to_underlying(Var::SLEEP)], path);
        WritePrivateProfileStringW(section, L"cl_yawspeed", g_state.cycle_vars_text[std::to_underlying(CycleVar::YAWSPEED)], path);
        WritePrivateProfileStringW(section, L"sensitivity", g_state.cycle_vars_text[std::to_underlying(CycleVar::SENSITIVITY)], path);
        WritePrivateProfileStringW(section, L"cl_anglespeedkey", g_state.cycle_vars_text[std::to_underlying(CycleVar::ANGLESPEEDKEY)], path);
        WritePrivateProfileStringW(section, L"m_yaw", g_state.cycle_vars_text[std::to_underlying(CycleVar::YAW)], path);
        win32::WritePrivateProfileIntW(section, L"enabled", g_state.switches[std::to_underlying(Switch::ENABLED)], path);
        win32::WritePrivateProfileIntW(section, L"raw_input", g_state.switches[std::to_underlying(Switch::RAW_INPUT)], path);
        win32::WritePrivateProfileIntW(section, L"current", g_state.current, path);
        win32::WritePrivateProfileIntW(section, L"selected", std::to_underlying(g_state.selected), path);
        WritePrivateProfileStructW(section, L"placement", &(*g_state.placement), sizeof(*g_state.placement), path);
    }
};

void State::on_settings_loaded()
{
    for (int i = 0; i < std::to_underlying(Var::COUNT); ++i) {
        parse_var(static_cast<Var>(i));
    }

    parse_cycle_vars();
    if (g_state.count > 0 && (g_state.current < 0 || g_state.current > g_state.count - 1)) {
        g_state.current = 0;
    }

    update_valid();

    if (std::to_underlying(g_state.selected) < 0 || g_state.selected > ConsoleItem::COUNT) {
        g_state.selected = static_cast<ConsoleItem>(0);
    }
    while (!g_state.developer && is_developer_console_item(g_state.selected)) {
        g_state.selected = static_cast<ConsoleItem>((std::to_underlying(g_state.selected) + 1) % (std::to_underlying(ConsoleItem::COUNT) + 1));
    }

    App::console->on_settings_loaded();
    App::console_window->on_settings_loaded();
}

void State::on_settings_save()
{
    App::console_window->on_settings_save();
}

void State::on_developer_changed()
{
    while (!g_state.developer && is_developer_console_item(g_state.selected)) {
        g_state.selected = static_cast<ConsoleItem>((std::to_underlying(g_state.selected) + 1) % (std::to_underlying(ConsoleItem::COUNT) + 1));
    }

    App::console->on_developer_changed();
}

void State::on_var_changed(Var var)
{
    parse_var(var);
    update_valid();
}

void State::on_cycle_vars_changed()
{
    auto prev_count = g_state.count;
    parse_cycle_vars();
    if (g_state.count != prev_count) {
        g_state.current = 0;
    }

    update_valid();
}

void State::on_switch_changed(Switch switch_)
{
    App::input->on_switch_changed(switch_);
    App::console->on_switch_changed(switch_);
    App::console_window->on_switch_changed(switch_);
}

void State::on_current_changed()
{
    App::console->on_current_changed();
}

void State::on_valid_updated()
{
    App::console_window->on_valid_updated();
}

void State::on_selected_changed(ConsoleItem prev)
{
    App::console->on_selected_changed(prev);
}

void State::update_valid()
{
    g_state.valid = (g_state.count > 0);
    for (int i = 0; (i < std::to_underlying(Var::COUNT)) && (g_state.valid); ++i) {
        g_state.valid = (g_state.valid && g_state.vars[i]);
    }
    on_valid_updated();
}

void State::parse_var(Var var)
{
    double d;
    auto p = parse_double(g_state.vars_text[std::to_underlying(var)], d);

    if (std::isnan(d) || d < 0.0 || *p != L'\0') {
        g_state.vars[std::to_underlying(var)] = std::nullopt;
        return;
    }

    g_state.vars[std::to_underlying(var)] = d;
}

void State::parse_cycle_vars()
{
    g_state.count = 0;
    for (int i = 0; i < std::to_underlying(CycleVar::COUNT); ++i) {
        g_state.cycle_vars[i].clear();
        const wchar_t *p = g_state.cycle_vars_text[i];
        do {
            double d;
            p = parse_double(p, d);

            if (std::isnan(d) || d < 0.0) {
                g_state.count = 0;
                return;
            }

            g_state.cycle_vars[i].push_back(d);

            if (*p == L'\0') {
                if (g_state.count <= 1) {
                    g_state.count = g_state.cycle_vars[i].size();
                } else if (g_state.cycle_vars[i].size() != 1 && g_state.count != g_state.cycle_vars[i].size()) {
                    g_state.count = 0;
                    return;
                }
                break;
            }

            if (!std::isspace(*(p - 1))) {
                g_state.count = 0;
                return;
            }
        } while (true);
    }
}

int main(int argc, char *argv[])
{
    App::run();
    return 0;
}
