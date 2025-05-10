#include <cmath>
#include <optional>
#include <utility>
#include <pathcch.h>
#include "common.hpp"
#include "resources.hpp"
#include "config.hpp"
#include "mouse_move_state.hpp"

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
    case ConsoleItem::RATE:
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
    default:
        return Switch::COUNT;
    }
}

struct State
{
    bool developer;

    int binds[std::to_underlying(Bind::COUNT)];
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

    void on_config_loaded(Config &config);
    void on_config_save(Config &config);
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

    void resize()
    {
        out.set_window_info(true, {0, 0, 0, 0});
        out.set_buffer_size({1, 1});
        out.set_window_info(true, {0, 0, BUFFER_WIDTH - 1, static_cast<SHORT>(BUFFER_HEIGHT[g_state.developer] - 1)});
        out.set_buffer_size({BUFFER_WIDTH, static_cast<SHORT>(BUFFER_HEIGHT[g_state.developer])});
        out.set_window_info(true, {0, 0, BUFFER_WIDTH - 1, static_cast<SHORT>(BUFFER_HEIGHT[g_state.developer] - 1)});
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

private:
    struct {
        std::array<bool, win32::VIRTUAL_KEYS> down;

        void start()
        {
            for (int vk = 0; vk < std::size(down); ++vk) {
                down[vk] = win32::is_key_down(vk);
            }
        }

        std::optional<int> run()
        {
            for (int vk = 0; vk < std::size(down); ++vk) {
                if (vk == VK_RETURN) {
                    continue;
                }

                auto prev_down = down[vk];
                down[vk] = win32::is_key_down(vk);
                if (!prev_down && down[vk]) {
                    auto scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
                    if (scan != 0 && vk != MapVirtualKeyW(scan, MAPVK_VSC_TO_VK_EX)) {
                        continue;
                    }
                    return vk;
                }
            }

            return std::nullopt;
        }
    } bind_capturer;

public:
    bool run()
    {
        bool ignore_input_events = false;

        if (editing && console_item_type(g_state.selected) == ConsoleItemType::BIND) {
            ignore_input_events = true;

            auto captured = bind_capturer.run();
            if (captured) {
                if (captured != VK_ESCAPE) {
                    g_state.binds[std::to_underlying(to_bind(g_state.selected))] = *captured;
                }
                editing = false;
                redraw_item_value(g_state.selected);
            }
        }

        do {
            INPUT_RECORD events[1024];
            auto count = in.consume_input_events(events, std::size(events));
            if (count == 0) {
                break;
            }

            for (int i = 0; !ignore_input_events && i < count; ++i) {
                switch (events[i].EventType) {
                case WINDOW_BUFFER_SIZE_EVENT:
                    redraw_full();
                    break;
                case KEY_EVENT:
                    switch (events[i].Event.KeyEvent.wVirtualKeyCode) {
                    case VK_ESCAPE:
                        if (events[i].Event.KeyEvent.bKeyDown) {
                            return false;
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
                                bind_capturer.start();

                                ignore_input_events = true;
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

        return true;
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
        L"rate",
        L"sleep"
    };
    static constexpr const wchar_t *SELECTOR[] = {L"   ", L" » "};
    static constexpr SHORT BUFFER_WIDTH = 64;
    static constexpr SHORT BUFFER_HEIGHT[] = {17, 20};
    static constexpr auto INPUT_PADDING = 18;

    const VersionInfo &version_info;

    DWORD initial_in_mode;

    COORD positions_selector[std::to_underlying(ConsoleItem::COUNT)];
    COORD positions_value[std::to_underlying(ConsoleItem::COUNT)];

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

struct App
{
    inline static std::optional<VersionInfo> version_info;
    inline static std::optional<Console> console;

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

        version_info.emplace(image_path);
        auto icon = load_icon_resource(instance);

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
        cls.lpszClassName = version_info->name;
        RegisterClassExW(&cls);

        auto hwnd = CreateWindowExW(0, version_info->name, version_info->name, 0, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr);

        console.emplace(*version_info);

        auto console_hwnd = GetConsoleWindow();
        SendMessage(console_hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        SendMessage(console_hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
        SetWindowLong(console_hwnd, GWL_STYLE, GetWindowLong(console_hwnd, GWL_STYLE) & ~WS_SIZEBOX & ~WS_MAXIMIZEBOX);

        win32::CursorVisibilityObserver cursor_visibility_observer;
        win32::CtrlSignalHandler ctrl_signal_handler(hwnd);

        Config config(ini_path, version_info->name);
        g_state.on_config_loaded(config);

        if (g_state.placement) {
            SetWindowPlacement(console_hwnd, &(*g_state.placement));
        }
        console->resize();
        reset_console_window_title();
        ShowWindow(console_hwnd, SW_SHOWNORMAL);

        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        win32::set_timer_resolution(1);

        bool active = false;
        std::array<bool, std::to_underlying(Bind::COUNT)> binds_down;
        MouseMoveState mouse_move_state;

        do {
        peek:
            MSG msg;
            if (PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    break;
                }
                DispatchMessage(&msg);
                goto peek;
            }

            if (!console->run()) {
                break;
            }

            auto prev_active = active;
            active =
                !console->editing &&
                g_state.switches[std::to_underlying(Switch::ENABLED)] &&
                g_state.valid &&
                !cursor_visibility_observer.visible();

            if (!active) {
                HANDLE handles[] = {console->in.handle};
                MsgWaitForMultipleObjects(std::size(handles), handles, false, INFINITE, QS_ALLINPUT);
                continue;
            }

            decltype(binds_down) prev_binds_down;
            if (!prev_active) {
                for (int i = 0; i < std::size(g_state.binds); ++i) {
                    binds_down[i] = win32::is_key_down(g_state.binds[i]);
                }
                prev_binds_down = binds_down;
            } else {
                prev_binds_down = binds_down;
                for (int i = 0; i < std::size(g_state.binds); ++i) {
                    binds_down[i] = win32::is_key_down(g_state.binds[i]);
                }
            }

            if (!prev_binds_down[std::to_underlying(Bind::CYCLE)] && binds_down[std::to_underlying(Bind::CYCLE)]) {
                g_state.current = (g_state.current + 1) % g_state.count;
                g_state.on_current_changed();
            }

            auto amount = mouse_move_state.update(
                !prev_active,
                binds_down[std::to_underlying(Bind::LEFT)],
                binds_down[std::to_underlying(Bind::RIGHT)],
                binds_down[std::to_underlying(Bind::SPEED)],
                *g_state.vars[std::to_underlying(Var::RATE)],
                g_state.cycle_vars[std::to_underlying(CycleVar::YAWSPEED)][g_state.current % g_state.cycle_vars[std::to_underlying(CycleVar::YAWSPEED)].size()],
                g_state.cycle_vars[std::to_underlying(CycleVar::ANGLESPEEDKEY)][g_state.current % g_state.cycle_vars[std::to_underlying(CycleVar::ANGLESPEEDKEY)].size()],
                g_state.cycle_vars[std::to_underlying(CycleVar::SENSITIVITY)][g_state.current % g_state.cycle_vars[std::to_underlying(CycleVar::SENSITIVITY)].size()],
                g_state.cycle_vars[std::to_underlying(CycleVar::YAW)][g_state.current % g_state.cycle_vars[std::to_underlying(CycleVar::YAW)].size()]);
            if (amount != 0) {
                win32::move_mouse_by(amount, 0);
            }

            win32::delay_execution_by(*g_state.vars[std::to_underlying(Var::SLEEP)]);
        } while (true);

        console.reset();

        g_state.placement = {};
        GetWindowPlacement(console_hwnd, &(*g_state.placement));

        g_state.on_config_save(config);
        config.save(ini_path, version_info->name);

        ctrl_signal_handler.done();
    }

    static void reset_console_window_title()
    {
        wchar_t buffer[128];
        std::swprintf(buffer, std::size(buffer), L"%s%s", version_info->title, (!g_state.valid ? L" (error)" : (g_state.switches[std::to_underlying(Switch::ENABLED)] ? L"" : L" (disabled)")));
        SetConsoleTitleW(buffer);
    }
};

void State::on_config_loaded(Config &config)
{
    g_state.developer = config.developer;
    g_state.binds[std::to_underlying(Bind::LEFT)] = config.bind_left;
    g_state.binds[std::to_underlying(Bind::RIGHT)] = config.bind_right;
    g_state.binds[std::to_underlying(Bind::SPEED)] = config.bind_speed;
    g_state.binds[std::to_underlying(Bind::CYCLE)] = config.bind_cycle;
    std::wcscpy(g_state.vars_text[std::to_underlying(Var::RATE)], config.rate);
    std::wcscpy(g_state.vars_text[std::to_underlying(Var::SLEEP)], config.sleep);
    std::wcscpy(g_state.cycle_vars_text[std::to_underlying(CycleVar::YAWSPEED)], config.cl_yawspeed);
    std::wcscpy(g_state.cycle_vars_text[std::to_underlying(CycleVar::SENSITIVITY)], config.sensitivity);
    std::wcscpy(g_state.cycle_vars_text[std::to_underlying(CycleVar::ANGLESPEEDKEY)], config.cl_anglespeedkey);
    std::wcscpy(g_state.cycle_vars_text[std::to_underlying(CycleVar::YAW)], config.m_yaw);
    g_state.switches[std::to_underlying(Switch::ENABLED)] = config.enabled;
    g_state.current = config.current;
    g_state.selected = static_cast<ConsoleItem>(config.selected);
    g_state.placement = config.placement;

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
}

void State::on_config_save(Config &config)
{
    config.developer = g_state.developer;
    config.bind_left = g_state.binds[std::to_underlying(Bind::LEFT)];
    config.bind_right = g_state.binds[std::to_underlying(Bind::RIGHT)];
    config.bind_speed = g_state.binds[std::to_underlying(Bind::SPEED)];
    config.bind_cycle = g_state.binds[std::to_underlying(Bind::CYCLE)];
    std::wcscpy(config.rate, g_state.vars_text[std::to_underlying(Var::RATE)]);
    std::wcscpy(config.sleep, g_state.vars_text[std::to_underlying(Var::SLEEP)]);
    std::wcscpy(config.cl_yawspeed, g_state.cycle_vars_text[std::to_underlying(CycleVar::YAWSPEED)]);
    std::wcscpy(config.sensitivity, g_state.cycle_vars_text[std::to_underlying(CycleVar::SENSITIVITY)]);
    std::wcscpy(config.cl_anglespeedkey, g_state.cycle_vars_text[std::to_underlying(CycleVar::ANGLESPEEDKEY)]);
    std::wcscpy(config.m_yaw, g_state.cycle_vars_text[std::to_underlying(CycleVar::YAW)]);
    config.enabled = g_state.switches[std::to_underlying(Switch::ENABLED)];
    config.current = g_state.current;
    config.selected = std::to_underlying(g_state.selected);
    config.placement = g_state.placement;
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
    App::console->on_switch_changed(switch_);

    switch (switch_) {
    case Switch::ENABLED:
        App::reset_console_window_title();
        break;
    }
}

void State::on_current_changed()
{
    App::console->on_current_changed();
}

void State::on_valid_updated()
{
    App::reset_console_window_title();
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

    switch (var) {
    case Var::RATE:
        if (d == 0.0) {
            g_state.vars[std::to_underlying(var)] = std::nullopt;
            return;
        }
        break;
    case Var::SLEEP:
        if (d >= 1000000.0) {
            g_state.vars[std::to_underlying(var)] = std::nullopt;
            return;
        }
        break;
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
