#include <stdio.h>
#include <windows.h>
#include <hidusage.h>
#include <algorithm>

namespace win32 {

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

long long performance_counter()
{
    LARGE_INTEGER i;
    QueryPerformanceCounter(&i);
    return i.QuadPart;
}

void set_timer_resolution(unsigned long hns)
{
    ULONG actual;
    ZwSetTimerResolution(hns, TRUE, &actual);
}

void delay_execution_by(long long hns)
{
    LARGE_INTEGER interval;
    interval.QuadPart = -1 * hns;
    NtDelayExecution(FALSE, &interval);
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

CURSORINFO get_mouse_cursor_info()
{
    CURSORINFO info = {};
    info.cbSize = sizeof(info);
    GetCursorInfo(&info);
    return info;
}

bool get_vk_string(int vk, wchar_t *result, size_t size)
{
    UINT scan_code;
    switch (vk) {
    case VK_LBUTTON:
        swprintf(result, size, L"Left Mouse Button");
        return true;
    case VK_RBUTTON:
        swprintf(result, size, L"Right Mouse Button");
        return true;
    case VK_MBUTTON:
        swprintf(result, size, L"Middle Mouse Button");
        return true;
    case VK_XBUTTON1:
        swprintf(result, size, L"X1 Mouse Button");
        return true;
    case VK_XBUTTON2:
        swprintf(result, size, L"X2 Mouse Button");
        return true;
    case VK_LEFT: case VK_UP: case VK_RIGHT: case VK_DOWN: case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME: case VK_INSERT: case VK_DELETE: case VK_DIVIDE: case VK_NUMLOCK:
        scan_code = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) | 0x100;
        break;
    default:
        scan_code = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        break;
    }
    return GetKeyNameTextW(scan_code << 16, result, size);
}

size_t get_raw_input_messages(const RAWINPUT &input, USHORT *vks, UINT *messages, size_t size)
{
    constexpr int MOUSE_DOWN[5] = {RI_MOUSE_BUTTON_1_DOWN, RI_MOUSE_BUTTON_2_DOWN, RI_MOUSE_BUTTON_3_DOWN, RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_5_DOWN};
    constexpr int MOUSE_UP[5] = {RI_MOUSE_BUTTON_1_UP, RI_MOUSE_BUTTON_2_UP, RI_MOUSE_BUTTON_3_UP, RI_MOUSE_BUTTON_4_UP, RI_MOUSE_BUTTON_5_UP};
    constexpr int MOUSE_VK[5] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};

    if (size == 0) {
        return 0;
    }

    size_t count = 0;
    switch (input.header.dwType) {
    case RIM_TYPEMOUSE:
        for (size_t i = 0; i < sizeof(MOUSE_DOWN) / sizeof(MOUSE_DOWN[0]) && count < size; ++i) {
            if (input.data.mouse.usButtonFlags & MOUSE_DOWN[i]) {
                vks[count] = MOUSE_VK[i];
                messages[count] = WM_KEYDOWN;
                ++count;
            } else if (input.data.mouse.usButtonFlags & MOUSE_UP[i]) {
                vks[count] = MOUSE_VK[i];
                messages[count] = WM_KEYUP;
                ++count;
            }
        }
        break;
    case RIM_TYPEKEYBOARD:
        switch (input.data.keyboard.VKey) {
        case VK_CONTROL:
            if (input.header.hDevice == NULL) {
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
        messages[count] = input.data.keyboard.Message;
        ++count;
        break;
    }
    return count;
}

struct Window
{
    HWND hwnd;

    Window(const wchar_t *class_name, const wchar_t *window_name)
    {
        HINSTANCE instance = GetModuleHandle(NULL);

        WNDCLASSEXW cls = {};
        cls.cbSize = sizeof(WNDCLASSEX);
        cls.lpfnWndProc = &proc;
        cls.hInstance = instance;
        cls.lpszClassName = class_name;
        RegisterClassExW(&cls);

        hwnd = CreateWindowExW(0, class_name, window_name, 0, 0, 0, 0, 0, NULL, NULL, instance, NULL);
    }

    static void wait_msg()
    {
        WaitMessage();
    }

    bool consume_msg(MSG &msg)
    {
        if (!PeekMessageW(&msg, hwnd, NULL, NULL, PM_REMOVE)) {
            return false;
        }
        DispatchMessage(&msg);
        return true;
    }

private:
    static LRESULT CALLBACK proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        switch (uMsg) {
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
};

struct RawInput
{
    RawInput(HWND target)
    {
        RAWINPUTDEVICE devices[2];

        devices[0].usUsagePage = HID_USAGE_PAGE_GENERIC;
        devices[0].usUsage = HID_USAGE_GENERIC_MOUSE;
        devices[0].dwFlags = RIDEV_INPUTSINK | RIDEV_NOLEGACY;
        devices[0].hwndTarget = target;

        devices[1].usUsagePage = HID_USAGE_PAGE_GENERIC;
        devices[1].usUsage = HID_USAGE_GENERIC_KEYBOARD;
        devices[1].dwFlags = RIDEV_INPUTSINK | RIDEV_NOLEGACY;
        devices[1].hwndTarget = target;

        RegisterRawInputDevices(devices, sizeof(devices) / sizeof(devices[0]), sizeof(devices[0]));
    }

    bool get_input(MSG msg, RAWINPUT &input)
    {
        UINT size;
        return GetRawInputData(reinterpret_cast<HRAWINPUT>(msg.lParam), RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) != -1;
    }
};

struct ConsoleInput
{
    HANDLE handle;

    ConsoleInput(HANDLE handle) : handle(handle) {}

    void set_mode(DWORD mode)
    {
        SetConsoleMode(handle, mode);
    }

    bool consume_input(INPUT_RECORD &input)
    {
        DWORD count;
        GetNumberOfConsoleInputEvents(handle, &count);
        if (count == 0) {
            return false;
        }
        ReadConsoleInputW(handle, &input, 1, &count);
        return true;
    }
};

struct ConsoleOutput
{
    HANDLE handle;

    ConsoleOutput(HANDLE handle) : handle(handle) {}

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

    CONSOLE_SCREEN_BUFFER_INFO get_screen_buffer_info()
    {
        CONSOLE_SCREEN_BUFFER_INFO info;
        GetConsoleScreenBufferInfo(handle, &info);
        return info;
    }

    void set_cursor_position(COORD position)
    {
        SetConsoleCursorPosition(handle, position);
    }

    void fill(wchar_t c, COORD position, DWORD count)
    {
        DWORD written;
        FillConsoleOutputCharacterW(handle, c, count, position, &written);
    }
};

}

int main(int argc, char *argv[])
{
    constexpr auto VERSION_STRING = L"1.1.0";
    constexpr auto SLEEP_INTERVAL_HNS = 2500;
    constexpr auto ACTIVE_CHECK_INTERVAL = 1.0 / 20.0;
    constexpr auto SLOWDOWN_FACTOR = 2.0 / 3.0;
    constexpr auto RATE_INCREASE = 50;
    constexpr auto RATE_MIN = 50;
    constexpr auto RATE_MAX = 2000;
    constexpr auto SPEED_INCREASE = 100;
    constexpr auto SPEED_MIN = 100;
    constexpr auto SPEED_MAX = 15000;

    UINT binds[3] = {VK_LBUTTON, VK_RBUTTON, VK_LSHIFT};
    bool active = false;
    int rate = 1000;
    int speed = 5000;

    auto frequency = win32::performance_counter_frequency();
    win32::Window window(L"turnbinds", L"turnbinds");
    win32::RawInput raw_input(window.hwnd);
    win32::ConsoleOutput out(GetStdHandle(STD_OUTPUT_HANDLE));
    win32::ConsoleInput in(GetStdHandle(STD_INPUT_HANDLE));

    win32::set_timer_resolution(1);
    in.set_mode(ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT);

    auto initial_cursor_info = out.get_cursor_info();
    auto initial_cursor_position = out.get_screen_buffer_info().dwCursorPosition;

    {
        auto info = initial_cursor_info;
        info.bVisible = FALSE;
        out.set_cursor_info(info);
    }

    bool last_down[3] = {false, false, false};
    bool down[3] = {false, false, false};
    long long active_check_time = -1;
    double mouse_remaining = 0.0;
    long long mouse_time = -1;

    bool quit = false;
    bool redraw = true;
    int rebinding = -1;
    auto current = win32::performance_counter();

    while (true) {
        for (size_t i = 0; i < sizeof(binds) / sizeof(binds[0]); ++i) {
            last_down[i] = down[i];
        }
        MSG msg;
        while (!quit && window.consume_msg(msg)) {
            switch (msg.message) {
            case WM_QUIT:
                quit = true;
                break;
            case WM_INPUT:
                RAWINPUT input;
                if (!raw_input.get_input(msg, input)) {
                    break;
                }

                USHORT vks[5];
                UINT messages[5];
                auto count = win32::get_raw_input_messages(input, vks, messages, sizeof(vks) / sizeof(vks[0]));

                for (size_t i = 0; i < count; ++i) {
                    if (rebinding == -1) {
                        for (size_t j = 0; j < sizeof(binds) / sizeof(binds[0]); ++j) {
                            if (binds[j] == vks[i]) {
                                down[j] = (messages[i] == WM_KEYDOWN) || (messages[i] == WM_KEYUP ? false : down[j]);
                            }
                        }
                    } else if (messages[i] == WM_KEYDOWN) {
                        binds[rebinding++] = vks[i];
                        if (rebinding == sizeof(binds) / sizeof(binds[0])) {
                            rebinding = -1;
                            for (size_t i = 0; i < sizeof(binds) / sizeof(binds[0]); ++i) {
                                down[i] = false;
                            }
                            active_check_time = -1;
                            active = false;
                            current = win32::performance_counter();
                        }
                        redraw = true;
                    }
                }

                break;
            }
        }

        INPUT_RECORD input;
        while (!quit && in.consume_input(input)) {
            const auto &key = input.Event.KeyEvent;
            switch (input.EventType) {
            case KEY_EVENT:
                switch (key.wVirtualKeyCode) {
                case 'C':
                    if (key.bKeyDown && key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) {
                        quit = true;
                    }
                    break;
                case VK_ESCAPE:
                    if (key.bKeyDown) {
                        quit = true;
                    }
                    break;
                default:
                    if (rebinding != -1) {
                        break;
                    }
                    switch (key.wVirtualKeyCode) {
                    case VK_UP:
                        if (key.bKeyDown) {
                            speed = std::min(SPEED_MAX, speed + (key.dwControlKeyState & SHIFT_PRESSED ? 1 : SPEED_INCREASE));
                            redraw = true;
                        }
                        break;
                    case VK_DOWN:
                        if (key.bKeyDown) {
                            speed = std::max(SPEED_MIN, speed - (key.dwControlKeyState & SHIFT_PRESSED ? 1 : SPEED_INCREASE));
                            redraw = true;
                        }
                        break;
                    case VK_RIGHT:
                        if (key.bKeyDown) {
                            rate = std::min(RATE_MAX, rate + (key.dwControlKeyState & SHIFT_PRESSED ? 1 : RATE_INCREASE));
                            redraw = true;
                        }
                        break;
                    case VK_LEFT:
                        if (key.bKeyDown) {
                            rate = std::max(RATE_MIN, rate - (key.dwControlKeyState & SHIFT_PRESSED ? 1 : RATE_INCREASE));
                            redraw = true;
                        }
                        break;
                    case 'R':
                        if (key.bKeyDown) {
                            rebinding = 0;
                            redraw = true;
                        }
                        break;
                    }
                    break;
                }
                break;
            }
        }

        if (quit) {
            break;
        }

        if (redraw) {
            redraw = false;

            auto info = out.get_screen_buffer_info();
            out.fill(0, initial_cursor_position, info.dwSize.X * (initial_cursor_position.Y - info.dwCursorPosition.Y + 1) - info.dwCursorPosition.X);

            out.set_cursor_position(initial_cursor_position);

            wprintf(L"\nturnbinds %s\nhttps://github.com/t5mat/turnbinds\n\n", VERSION_STRING);

            {
                constexpr const wchar_t *BIND_NAMES[] = {L"+left", L"+right", L"+speed"};

                size_t count = (rebinding == -1 ? sizeof(binds) / sizeof(binds[0]) : rebinding + 1);
                for (size_t i = 0; i < count; ++i) {
                    wchar_t key[128] = L"?";
                    if (rebinding == -1 || i < count - 1) {
                        win32::get_vk_string(binds[i], key, sizeof(key));
                    }
                    wprintf(L"%s = %s\n", BIND_NAMES[i], key);
                }
            }

            if (rebinding == -1) {
                wprintf(L"\n[%s]\n* Rate: %dhz\n* Speed: %d\n", active ? L"active" : L"inactive", rate, speed);
            }
        }

        if (rebinding != -1) {
            window.wait_msg();
            continue;
        }

        bool changed_to_active = false;
        if (current - active_check_time >= frequency * ACTIVE_CHECK_INTERVAL) {
            active_check_time = current;
            bool new_active = (win32::get_mouse_cursor_info().flags == 0);
            if (active != new_active) {
                active = new_active;
                changed_to_active = active;
                redraw = true;
            }
        }

        if (changed_to_active || (last_down[0] ^ down[0]) || (last_down[1] ^ down[1])) {
            mouse_time = current;
            mouse_remaining = 0.0;
        }

        {
            double delay = SLEEP_INTERVAL_HNS - (win32::performance_counter() - current) / (frequency / 1000000.0);
            if (delay > 0.0) {
                win32::delay_execution_by(delay);
            }
        }
        current = win32::performance_counter();

        if (active && down[0] ^ down[1] && current - mouse_time >= frequency * (1.0 / rate)) {
            mouse_remaining += (int(down[0]) * -1 + int(down[1])) * (speed * (down[2] ? SLOWDOWN_FACTOR : 1.0)) * (current - mouse_time) / frequency;
            auto amount = static_cast<long long>(mouse_remaining);
            win32::move_mouse_by(amount, 0);
            mouse_remaining -= amount;
            mouse_time = current;
        }
    }

    out.set_cursor_info(initial_cursor_info);

    return 0;
}
