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

bool is_key_down(int vk)
{
    return GetAsyncKeyState(vk) & 0x8000;
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

    RAWINPUT get_input(MSG msg)
    {
        RAWINPUT input;
        UINT inputSize;
        GetRawInputData(reinterpret_cast<HRAWINPUT>(msg.lParam), RID_INPUT, &input, &inputSize, sizeof(RAWINPUTHEADER));
        return input;
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

struct RawInputState
{
    bool mouse1;
    bool mouse2;
    bool lshift;

    void process(const RAWINPUT &input)
    {
        const auto &mouse = input.data.mouse;
        const auto &key = input.data.keyboard;

        switch (input.header.dwType) {
        case RIM_TYPEMOUSE:
            mouse1 = (mouse.usButtonFlags & RI_MOUSE_BUTTON_1_DOWN) || (mouse.usButtonFlags & RI_MOUSE_BUTTON_1_UP ? false : mouse1);
            mouse2 = (mouse.usButtonFlags & RI_MOUSE_BUTTON_2_DOWN) || (mouse.usButtonFlags & RI_MOUSE_BUTTON_2_UP ? false : mouse2);
            break;
        case RIM_TYPEKEYBOARD:
            switch (key.VKey) {
            case VK_SHIFT:
                if (key.MakeCode == 0x2a) {
                    lshift = (key.Message == WM_KEYDOWN) || (key.Message == WM_KEYUP ? false : lshift);
                }
                break;
            }
            break;
        }
    }

    void update()
    {
        mouse1 = win32::is_key_down(VK_LBUTTON);
        mouse2 = win32::is_key_down(VK_RBUTTON);
        lshift = win32::is_key_down(VK_LSHIFT);
    }
};

int main(int argc, char *argv[])
{
    constexpr auto VERSION_STRING = L"1.0.0";
    constexpr auto SLEEP_INTERVAL_HNS = 2500;
    constexpr auto ACTIVE_CHECK_INTERVAL = 1.0 / 20.0;
    constexpr auto SLOWDOWN_FACTOR = 2.0 / 3.0;
    constexpr auto RATE_INCREASE = 50;
    constexpr auto RATE_MIN = 50;
    constexpr auto RATE_MAX = 2000;
    constexpr auto SPEED_INCREASE = 100;
    constexpr auto SPEED_MIN = 100;
    constexpr auto SPEED_MAX = 15000;

    bool active = false;
    int rate = 1000;
    int speed = 5000;

    auto frequency = win32::performance_counter_frequency();
    win32::Window window(L"turnbinds", L"turnbinds");
    win32::RawInput raw_input(window.hwnd);
    win32::ConsoleOutput out(GetStdHandle(STD_OUTPUT_HANDLE));
    win32::ConsoleInput in(GetStdHandle(STD_INPUT_HANDLE));

    win32::set_timer_resolution(1);
    in.set_mode(ENABLE_WINDOW_INPUT);

    auto initial_cursor_info = out.get_cursor_info();
    auto initial_cursor_position = out.get_screen_buffer_info().dwCursorPosition;

    {
        auto info = initial_cursor_info;
        info.bVisible = FALSE;
        out.set_cursor_info(info);
    }

    RawInputState input_state;
    RawInputState last_input_state;
    long long active_check_time = -1;
    double mouse_remaining = 0.0;
    long long mouse_time = -1;

    input_state.update();
    auto current = win32::performance_counter();
    bool quit = false;
    bool redraw = true;

    while (true) {
        last_input_state = input_state;
        MSG msg;
        while (!quit && window.consume_msg(msg)) {
            switch (msg.message) {
            case WM_QUIT:
                quit = true;
                break;
            case WM_INPUT:
                input_state.process(raw_input.get_input(msg));
                break;
            }

        }

        INPUT_RECORD input;
        while (!quit && in.consume_input(input)) {
            const auto &key = input.Event.KeyEvent;
            switch (input.EventType) {
            case KEY_EVENT:
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
            wprintf(L"\nturnbinds %s\nhttps://github.com/t5mat/turnbinds\n\n[%s]\n* Rate: %dhz\n* Speed: %d\n", VERSION_STRING, active ? L"active" : L"inactive", rate, speed);
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

        if (changed_to_active) {
            input_state.update();
        }

        if (changed_to_active || (last_input_state.mouse1 ^ input_state.mouse1) || (last_input_state.mouse2 ^ input_state.mouse2)) {
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

        if (active && input_state.mouse1 ^ input_state.mouse2 && current - mouse_time >= frequency * (1.0 / rate)) {
            mouse_remaining +=
                (int(input_state.mouse1) * -1 + int(input_state.mouse2)) *
                (speed * (input_state.lshift ? SLOWDOWN_FACTOR : 1.0)) *
                (current - mouse_time) / frequency;
            auto amount = static_cast<long long>(mouse_remaining);
            win32::move_mouse_by(amount, 0);
            mouse_remaining -= amount;
            mouse_time = current;
        }
    }

    out.set_cursor_info(initial_cursor_info);

    return 0;
}
