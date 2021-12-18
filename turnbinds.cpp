#include <cmath>
#include <array>
#include <optional>
#include <vector>
#include <pathcch.h>
#include <windows.h>
#include <shellapi.h>
#include <windns.h>
#include <hidusage.h>

namespace common {

template<typename T>
constexpr std::enable_if_t<std::is_enum<T>::value, std::underlying_type_t<T>> operator*(T v)
{
    return std::underlying_type_t<T>(v);
}

wchar_t *wcsstrip(wchar_t *s)
{
    size_t size = wcslen(s);
    if (size == 0) {
        return s;
    }

    wchar_t *end = s + size - 1;
    while (end >= s && isspace(*end)) {
        end--;
    }
    *(end + 1) = L'\0';

    while ((*s != L'\0') && isspace(*s)) {
        s++;
    }

    return s;
}

const wchar_t *parse_double(const wchar_t *s, double &value)
{
    wchar_t *end;
    value = wcstod(s, &end);
    if (end == s) {
        value = NAN;
    }
    while (*end != L'\0') {
        if (!isspace(*end)) {
            break;
        }
        ++end;
    }
    return end;
}

namespace win32 {

namespace {

using ZwSetTimerResolution_t = NTSTATUS (WINAPI *)(IN ULONG RequestedResolution, IN BOOLEAN Set, OUT PULONG ActualResolution);
using NtDelayExecution_t = NTSTATUS (WINAPI *)(IN BOOL Alertable, IN PLARGE_INTEGER DelayInterval);

auto ZwSetTimerResolution = reinterpret_cast<ZwSetTimerResolution_t>(GetProcAddress(LoadLibraryW(L"ntdll.dll"), "ZwSetTimerResolution"));
auto NtDelayExecution = reinterpret_cast<NtDelayExecution_t>(GetProcAddress(LoadLibraryW(L"ntdll.dll"), "NtDelayExecution"));

long long performance_counter_frequency()
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
}

}

constexpr auto VIRTUAL_KEYS = 256;

const auto SHELL_TASKBAR_CREATED_MSG = RegisterWindowMessageW(L"TaskbarCreated");

const auto PERFORMANCE_COUNTER_FREQUENCY = performance_counter_frequency();

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
        swprintf(result, count, L"Left Mouse Button");
        return true;
    case VK_RBUTTON:
        swprintf(result, count, L"Right Mouse Button");
        return true;
    case VK_MBUTTON:
        swprintf(result, count, L"Middle Mouse Button");
        return true;
    case VK_XBUTTON1:
        swprintf(result, count, L"X1 Mouse Button");
        return true;
    case VK_XBUTTON2:
        swprintf(result, count, L"X2 Mouse Button");
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

size_t get_console_process_count()
{
    DWORD list[1];
    return GetConsoleProcessList(list, std::size(list));
}

CURSORINFO get_mouse_cursor_info()
{
    CURSORINFO info = {};
    info.cbSize = sizeof(info);
    GetCursorInfo(&info);
    return info;
}

HWND create_window(const wchar_t *class_name, const wchar_t *window_name, WNDPROC proc)
{
    auto instance = GetModuleHandle(nullptr);

    WNDCLASSEXW cls = {};
    cls.cbSize = sizeof(WNDCLASSEX);
    cls.lpfnWndProc = proc;
    cls.hInstance = instance;
    cls.lpszClassName = class_name;
    RegisterClassExW(&cls);

    return CreateWindowExW(0, class_name, window_name, 0, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
}

namespace {

struct ConsoleBase
{
    HANDLE handle;

    explicit ConsoleBase(HANDLE handle) : handle(handle) {}

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
    explicit ConsoleInput(HANDLE handle) : ConsoleBase(handle) {}

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
        size_t count = wcslen(s);
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

        if (buffer[read - 1] == '\n') {
            buffer[read - 2] = L'\0';
            return;
        }

        buffer[read - 1] = L'\0';

        wchar_t rest[1024];
        do {
            ReadConsoleW(handle, rest, std::size(rest), &read, nullptr);
        } while (rest[read - 1] != '\n');
    }
};

struct ConsoleOutput :
    ConsoleBase
{
    explicit ConsoleOutput(HANDLE handle) : ConsoleBase(handle) {}

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

    void set_screen_buffer_size(COORD size)
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
        WriteConsoleW(handle, s, wcslen(s), nullptr, nullptr);
    }

    void write_info(const wchar_t *s, size_t count, CONSOLE_SCREEN_BUFFER_INFOEX &info)
    {
        count = std::min(count, size_t(info.dwSize.X - info.dwCursorPosition.X));
        WriteConsoleW(handle, s, count, nullptr, nullptr);
        info.dwCursorPosition.X += count;
    }

    void write_text_info(const wchar_t *s, CONSOLE_SCREEN_BUFFER_INFOEX &info)
    {
        size_t count = std::min(wcslen(s), size_t(info.dwSize.X - info.dwCursorPosition.X));
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
    RAW_INPUT,
    COUNT
};

enum class ConsoleItemType
{
    BIND,
    CYCLE_VAR,
    SWITCH,
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
    case ConsoleItem::RAW_INPUT:
        return ConsoleItemType::SWITCH;
    default:
        return ConsoleItemType::COUNT;
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
    case ConsoleItem::RAW_INPUT:
        return Switch::RAW_INPUT;
    default:
        return Switch::COUNT;
    }
}

struct State
{
    bool enabled;
    double sleep;
    double rate;

    UINT binds[*Bind::COUNT];
    std::vector<double> cycle_vars[*CycleVar::COUNT];
    bool switches[*Switch::COUNT];
    int current;

    int count;

    wchar_t cycle_vars_text[*CycleVar::COUNT][128];
    ConsoleItem selected;

    void on_start();
    void on_cycle_vars_changed();
    void on_raw_input_changed();
    void on_current_changed();
    void on_selected_changed(ConsoleItem prev);

    void parse_cycle_vars();
} g_state;

struct Input
{
    std::array<bool, win32::VIRTUAL_KEYS> down;
    bool capturing = false;
    USHORT captured;
    bool binds = false;

    explicit Input(HWND hwnd) : hwnd(hwnd) {}

    void start_capturing()
    {
        capturing = true;

        if (g_state.switches[*Switch::RAW_INPUT]) {
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

        if (g_state.switches[*Switch::RAW_INPUT]) {
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

        if (g_state.switches[*Switch::RAW_INPUT]) {
            if (--raw_input == 0) {
                disable_raw_input();
            }
        }
    }

    void on_raw_input_changed()
    {
        if (!g_state.switches[*Switch::RAW_INPUT] && raw_input > 0) {
            raw_input = 0;
            disable_raw_input();
        }
        if (capturing) {
            start_capturing();
        }
        if (binds) {
            enable_binds();
        }
    }

    void run()
    {
        if (!g_state.switches[*Switch::RAW_INPUT]) {
            if (capturing) {
                for (int i = 0; i < std::size(down); ++i) {
                    bool last = down[i];
                    down[i] = win32::is_key_down(i);
                    if (!last && down[i]) {
                        if (i == VK_ESCAPE || i == VK_RETURN) {
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

            if (!g_state.switches[*Switch::RAW_INPUT]) {
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
        RegisterRawInputDevices(devices, std::size(devices), sizeof(devices[0]));
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
        RegisterRawInputDevices(devices, std::size(devices), sizeof(devices[0]));
    }
};

struct BindHandler
{
    void reset()
    {
        last_down = {};
    }

    void run(Input &input)
    {
        if (!last_down[*Bind::CYCLE] && input.down[g_state.binds[*Bind::CYCLE]]) {
            g_state.current = (g_state.current + 1) % g_state.count;
            g_state.on_current_changed();
        }

        auto time = win32::performance_counter();

        if ((last_down[*Bind::LEFT] ^ input.down[g_state.binds[*Bind::LEFT]]) || (last_down[*Bind::RIGHT] ^ input.down[g_state.binds[*Bind::RIGHT]])) {;
            last_time = time;
            remaining = 0.0;
        }

        for (int i = 0; i < std::size(last_down); ++i) {
            last_down[i] = input.down[g_state.binds[i]];
        }

        if ((input.down[g_state.binds[*Bind::LEFT]] ^ input.down[g_state.binds[*Bind::RIGHT]]) && time - last_time >= win32::PERFORMANCE_COUNTER_FREQUENCY * (1.0 / g_state.rate)) {
            double cycle_vars[*CycleVar::COUNT];
            for (int i = 0; i < std::size(cycle_vars); ++i) {
                cycle_vars[i] = g_state.cycle_vars[i][g_state.current % g_state.cycle_vars[i].size()];
            }

            remaining +=
                (
                    (int(input.down[g_state.binds[*Bind::LEFT]]) * -1 + int(input.down[g_state.binds[*Bind::RIGHT]])) *
                    (cycle_vars[*CycleVar::YAWSPEED] / (cycle_vars[*CycleVar::SENSITIVITY] * cycle_vars[*CycleVar::YAW])) *
                    (input.down[g_state.binds[*Bind::SPEED]] ? cycle_vars[*CycleVar::ANGLESPEEDKEY] : 1.0) *
                    (time - last_time)
                ) / win32::PERFORMANCE_COUNTER_FREQUENCY;

            auto amount = static_cast<long long>(remaining);
            if (amount != 0) {
                win32::move_mouse_by(amount, 0);
                remaining -= amount;
            }

            last_time = time;
        }
    }

private:
    long long last_time;
    double remaining;
    std::array<bool, *Bind::COUNT> last_down;
};

enum class WindowCommand
{
    RESTORE,
    ENABLED,
    EXIT,
};

const struct VersionInfo
{
    const wchar_t *name;
    const wchar_t *title;
    const wchar_t *version;
    const wchar_t *copyright;

    VersionInfo()
    {
        wchar_t path[PATHCCH_MAX_CCH];
        GetModuleFileNameW(nullptr, path, std::size(path));

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
} g_version_info;

struct Console
{
    win32::ConsoleOutput out;
    win32::ConsoleInput in;
    bool editing = false;

    Console() :
        out(CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, nullptr, CONSOLE_TEXTMODE_BUFFER, nullptr)),
        in(GetStdHandle(STD_INPUT_HANDLE))
    {
        initial_in_mode = in.get_mode();

        SMALL_RECT bounds{0, 0, INITIAL_BUFFER_SIZE.X + 1, INITIAL_BUFFER_SIZE.Y + 1};
        SetConsoleWindowInfo(out.handle, true, &bounds);

        auto info = out.get_screen_buffer_info();
        out.set_screen_buffer_size({
            static_cast<SHORT>(info.srWindow.Right - info.srWindow.Left + 1),
            static_cast<SHORT>(info.srWindow.Bottom - info.srWindow.Top + 1)
        });

        out.set_mode(ENABLE_PROCESSED_OUTPUT);
        out.set_as_active_screen_buffer();

        in.set_mode(ENABLE_PROCESSED_INPUT | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS);
    }

    ~Console()
    {
        in.set_mode(initial_in_mode);
    }

    void on_start()
    {
        redraw_full();
    }

    void on_raw_input_changed()
    {
        redraw_item_value(ConsoleItem::RAW_INPUT);
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
            size_t count = in.consume_input_events(events, std::size(events));
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
                            PostMessageW(hwnd, WM_COMMAND, *WindowCommand::EXIT, 0);
                        }
                        break;
                    case VK_UP:
                    case VK_DOWN:
                        if (events[i].Event.KeyEvent.bKeyDown) {
                            bool down = (events[i].Event.KeyEvent.wVirtualKeyCode == VK_DOWN);
                            if (events[i].Event.KeyEvent.bKeyDown) {
                                auto prev = g_state.selected;
                                g_state.selected = static_cast<ConsoleItem>((down ? (*g_state.selected + 1) : (*g_state.selected - 1 + *ConsoleItem::COUNT + 1)) % (*ConsoleItem::COUNT + 1));
                                g_state.on_selected_changed(prev);
                            }
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
                                g_state.switches[*to_switch(g_state.selected)] = right;
                                g_state.on_raw_input_changed();
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
                            case ConsoleItemType::CYCLE_VAR:
                                editing = true;

                                redraw_item_value(ConsoleItem::CL_YAWSPEED);
                                redraw_item_value(ConsoleItem::SENSITIVITY);
                                redraw_item_value(ConsoleItem::CL_ANGLESPEEDKEY);
                                redraw_item_value(ConsoleItem::M_YAW);

                                read_input_value(g_state.selected);

                                editing = false;

                                g_state.on_cycle_vars_changed();
                                redraw_full();
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

                g_state.binds[*to_bind(g_state.selected)] = input.captured;
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
        L"raw input"
    };
    static constexpr const wchar_t *SELECTOR[] = {L"   ", L" » "};
    static constexpr auto INITIAL_BUFFER_SIZE = COORD{64, 15};
    static constexpr auto INPUT_PADDING = 18;

    DWORD initial_in_mode;

    COORD positions_selector[*ConsoleItem::COUNT];
    COORD positions_value[*ConsoleItem::COUNT];

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

        out.set_cursor_position(positions_value[*item]);

        auto cycle_var = to_cycle_var(item);

        in.write_text(g_state.cycle_vars_text[*cycle_var]);

        auto count = std::size(g_state.cycle_vars_text[0]);
        wchar_t buffer[count];
        in.read_text(buffer, count);
        wcscpy(g_state.cycle_vars_text[*cycle_var], wcsstrip(buffer));

        out.set_cursor_info(prev_cursor_info);
        in.set_mode(prev_in_mode);
        out.set_mode(prev_out_mode);

        out.set_cursor_position(positions_value[*item]);
    }

    void redraw_full()
    {
        auto info = out.get_screen_buffer_info();
        auto initial_cursor = info.dwCursorPosition;

        size_t buffer_size = info.dwSize.X + 1;
        wchar_t buffer[buffer_size];

        {
            auto info = out.get_cursor_info();
            info.bVisible = false;
            out.set_cursor_info(info);
        }

        out.fill_attribute(0, {0, 0}, info.dwSize.X * info.dwSize.Y);

        out.set_cursor_position(info.dwCursorPosition = {0, 0});
        out.fill(L' ', info.dwCursorPosition, info.dwSize.X - info.dwCursorPosition.X);

        for (int i = 0; i < *ConsoleItem::COUNT; ++i) {
            auto type = console_item_type(static_cast<ConsoleItem>(i));

            out.set_cursor_position(info.dwCursorPosition = {0, static_cast<SHORT>(info.dwCursorPosition.Y + 1)});

            if (i == *ConsoleItem::CL_YAWSPEED || i == *ConsoleItem::RAW_INPUT) {
                out.fill(L' ', info.dwCursorPosition, info.dwSize.X - info.dwCursorPosition.X);
                out.set_cursor_position(info.dwCursorPosition = {0, static_cast<SHORT>(info.dwCursorPosition.Y + 1)});
            }

            positions_selector[i] = info.dwCursorPosition;
            out.write_text_info(SELECTOR[g_state.selected == static_cast<ConsoleItem>(i)], info);

            bool valid = (type != ConsoleItemType::CYCLE_VAR || g_state.count > 0);
            if (!valid) {
                out.set_text_attributes(FOREGROUND_INTENSITY | FOREGROUND_RED);
            }
            swprintf(buffer, buffer_size, L"%-*s", INPUT_PADDING, CONSOLE_ITEM_NAMES[i]);
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

            wchar_t title[INITIAL_BUFFER_SIZE.X + 1];
            swprintf(title, INITIAL_BUFFER_SIZE.X + 1, L"%s %s", g_version_info.title, g_version_info.version);
            swprintf(buffer, buffer_size, L"%*s", info.dwSize.X - 2, title);

            out.set_cursor_position(info.dwCursorPosition = {0, static_cast<SHORT>(info.dwCursorPosition.Y + 1)});
            out.write_text_info(buffer, info);
            out.fill(L' ', info.dwCursorPosition, info.dwSize.X - info.dwCursorPosition.X);

            swprintf(buffer, buffer_size, L"%*s", info.dwSize.X - 2, g_version_info.copyright);

            out.set_cursor_position(info.dwCursorPosition = {0, static_cast<SHORT>(info.dwCursorPosition.Y + 1)});
            out.write_text_info(buffer, info);

            out.set_text_attributes(info.wAttributes);
        }

        out.fill(L' ', info.dwCursorPosition, info.dwSize.X * info.dwSize.Y);

        out.set_cursor_position(initial_cursor);
    }

    void redraw_item_value(ConsoleItem item)
    {
        out.set_cursor_position(positions_value[*item]);
        auto info = out.get_screen_buffer_info();
        draw_item_value(item, info);
        out.fill(L' ', info.dwCursorPosition, info.dwSize.X - info.dwCursorPosition.X);
    }

    void redraw_selector(ConsoleItem prev)
    {
        if (prev != ConsoleItem::COUNT) {
            out.set_cursor_position(positions_selector[*prev]);
            out.write_text(SELECTOR[false]);
        }

        if (g_state.selected != ConsoleItem::COUNT) {
            out.set_cursor_position(positions_selector[*g_state.selected]);
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

                wchar_t buffer[INITIAL_BUFFER_SIZE.X + 1];
                if (!win32::get_vk_string(g_state.binds[*bind], buffer, std::size(buffer))) {
                    buffer[0] = L'\0';
                }

                out.write_text_info(buffer, info);
            }
            break;
        case ConsoleItemType::CYCLE_VAR:
            {
                if (editing && g_state.selected == item) {
                    break;
                }

                auto cycle_var = to_cycle_var(item);

                if (editing || g_state.count == 0) {
                    out.write_text_info(g_state.cycle_vars_text[*cycle_var], info);
                    break;
                }

                const wchar_t *start = g_state.cycle_vars_text[*cycle_var];
                for (int i = 0; i < g_state.cycle_vars[*cycle_var].size(); ++i) {
                    auto end = parse_double(start, g_state.cycle_vars[*cycle_var][i]);
                    auto current_end = end;
                    while (isspace(*(current_end - 1))) {
                        --current_end;
                    }

                    bool current = (g_state.cycle_vars[*cycle_var].size() != 1) && (i == g_state.current);

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

                if (!g_state.switches[*switch_]) {
                    out.set_text_attributes(BACKGROUND_RED | BACKGROUND_BLUE | FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                }
                out.write_text_info(L"off", info);
                if (!g_state.switches[*switch_]) {
                    out.set_text_attributes(info.wAttributes);
                }

                out.write_text_info(L" ", info);

                if (g_state.switches[*switch_]) {
                    out.set_text_attributes(BACKGROUND_RED | BACKGROUND_BLUE | FOREGROUND_INTENSITY | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                }
                out.write_text_info(L"on", info);
                if (g_state.switches[*switch_]) {
                    out.set_text_attributes(info.wAttributes);
                }
            }
            break;
        }
    }
};

struct TrayIcon
{
    explicit TrayIcon(HWND hwnd)
    {
        data = {};
        data.uVersion = NOTIFYICON_VERSION_4;
        data.cbSize = sizeof(data);
        data.hWnd = hwnd;
        data.uID = 1;
        data.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP | NIF_MESSAGE;
        data.uCallbackMessage = WINDOW_MSG;
        data.hIcon = static_cast<HICON>(::LoadImageW(GetModuleHandle(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
        wcscpy(data.szTip, g_version_info.title);
    }

    void show()
    {
        Shell_NotifyIconW(NIM_ADD, &data);
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }

    void hide()
    {
        Shell_NotifyIconW(NIM_DELETE, &data);
    }

    bool handle_msg(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        if (uMsg == win32::SHELL_TASKBAR_CREATED_MSG) {
            show();
            return true;
        }

        if (uMsg != WINDOW_MSG) {
            return false;
        }

        switch (LOWORD(lParam)) {
        case NIN_SELECT:
            PostMessageW(hwnd, WM_COMMAND, *WindowCommand::RESTORE, 0);
            break;
        case WM_CONTEXTMENU:
            {
                auto menu = CreatePopupMenu();
                AppendMenuW(menu, (g_state.enabled ? MF_CHECKED : MF_UNCHECKED), *WindowCommand::ENABLED, L"Enabled");
                AppendMenuW(menu, MF_STRING, *WindowCommand::EXIT, L"Exit");

                POINT point;
                GetCursorPos(&point);

                SetForegroundWindow(hwnd);
                TrackPopupMenuEx(menu, TPM_LEFTBUTTON | TPM_LEFTALIGN | TPM_BOTTOMALIGN, point.x, point.y, hwnd, nullptr);
                PostMessageW(hwnd, WM_NULL, 0, 0);

                DestroyMenu(menu);
            }
            break;
        }

        return true;
    }

private:
    static constexpr auto WINDOW_MSG = WM_USER + 0;

    NOTIFYICONDATAW data;
};

struct App
{
    inline static std::optional<Input> input;
    inline static std::optional<Console> console;
    inline static std::optional<TrayIcon> tray_icon;

    static void run()
    {
        win32::set_timer_resolution(1);

        hwnd = win32::create_window(g_version_info.name, g_version_info.name, window_proc);
        console_hwnd = GetConsoleWindow();

        SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZESTART, nullptr, event_proc_minimize, 0, 0, WINEVENT_OUTOFCONTEXT);

        input.emplace(hwnd);

        console.emplace();

        if (win32::get_console_process_count() == 1) {
            SetWindowLong(console_hwnd, GWL_STYLE, GetWindowLong(console_hwnd, GWL_STYLE) & ~WS_SIZEBOX & ~WS_MAXIMIZEBOX);
            tray_icon.emplace(hwnd);
        }

        BindHandler bind_handler;

        event = CreateEventW(nullptr, false, false, nullptr);
        SetConsoleCtrlHandler(ctrl_signal_handler, true);

        SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE, nullptr, event_proc_cursor, 0, 0, WINEVENT_OUTOFCONTEXT);
        cursor_visible = (win32::get_mouse_cursor_info().flags & CURSOR_SHOWING);

        g_state.on_start();

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
            active = (!console->editing && g_state.enabled && g_state.count > 0 && !cursor_visible);

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
                bind_handler.reset();
            }

            bind_handler.run(*input);
            win32::delay_execution_by(g_state.sleep);
        } while (true);

        if (tray_icon) {
            tray_icon->hide();
        }

        console.reset();

        SetEvent(event);
    }

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg) {
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_COMMAND:
            switch (static_cast<WindowCommand>(LOWORD(wParam))) {
                case WindowCommand::RESTORE:
                    ShowWindow(console_hwnd, SW_RESTORE);
                    SetForegroundWindow(console_hwnd);
                    break;
                case WindowCommand::ENABLED:
                    g_state.enabled = !g_state.enabled;
                    break;
                case WindowCommand::EXIT:
                    PostMessageW(hwnd, WM_QUIT, 0, 0);
                    break;
            }
            return 0;
        default:
            if (tray_icon && tray_icon->handle_msg(hwnd, uMsg, wParam, lParam)) {
                return 0;
            }
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
        return 0;
    }

    static void event_proc_minimize(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD idEventThread, DWORD dwmsEventTime)
    {
        switch (event) {
        case EVENT_SYSTEM_MINIMIZESTART:
            if (hwnd == console_hwnd) {
                ShowWindow(hwnd, SW_HIDE);
            }
            break;
        }
    }

    static void event_proc_cursor(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD idEventThread, DWORD dwmsEventTime)
    {
        if (idObject == OBJID_CURSOR) {
            if (event == EVENT_OBJECT_HIDE) {
                cursor_visible = false;
            } else if (event == EVENT_OBJECT_SHOW) {
                cursor_visible = true;
            }
        }
    }

    static BOOL ctrl_signal_handler(DWORD dwCtrlType)
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

private:
    inline static HWND hwnd;
    inline static HWND console_hwnd;
    inline static HANDLE event;
    inline static bool cursor_visible;
};

void State::on_start()
{
    g_state.enabled = true;
    g_state.sleep = 5000.0;
    g_state.rate = 1000.0;
    g_state.binds[*Bind::LEFT] = VK_LBUTTON;
    g_state.binds[*Bind::RIGHT] = VK_RBUTTON;
    g_state.binds[*Bind::SPEED] = VK_LSHIFT;
    g_state.binds[*Bind::CYCLE] = VK_XBUTTON1;
    wcscpy(g_state.cycle_vars_text[*CycleVar::YAWSPEED], L"75 120 210");
    wcscpy(g_state.cycle_vars_text[*CycleVar::SENSITIVITY], L"1.0");
    wcscpy(g_state.cycle_vars_text[*CycleVar::ANGLESPEEDKEY], L"0.67");
    wcscpy(g_state.cycle_vars_text[*CycleVar::YAW], L"0.022");
    g_state.switches[*Switch::RAW_INPUT] = false;
    g_state.current = 0;
    g_state.selected = static_cast<ConsoleItem>(0);
    parse_cycle_vars();

    if (App::tray_icon) {
        App::tray_icon->show();
    }
    App::console->on_start();
}

void State::on_cycle_vars_changed()
{
    auto prev_count = g_state.count;
    parse_cycle_vars();
    if (g_state.count != prev_count) {
        g_state.current = 0;
    }
}

void State::on_raw_input_changed()
{
    App::input->on_raw_input_changed();
    App::console->on_raw_input_changed();
}

void State::on_current_changed()
{
    App::console->on_current_changed();
}

void State::on_selected_changed(ConsoleItem prev)
{
    App::console->on_selected_changed(prev);
}

void State::parse_cycle_vars()
{
    g_state.count = 0;
    for (int i = 0; i < *CycleVar::COUNT; ++i) {
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

            if (!isspace(*(p - 1))) {
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
