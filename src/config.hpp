#pragma once

#include <optional>
#include "common.hpp"

struct Config
{
    bool developer;
    int bind_left;
    int bind_right;
    int bind_speed;
    int bind_cycle;
    wchar_t rate[128];
    wchar_t sleep[128];
    wchar_t cl_yawspeed[128];
    wchar_t sensitivity[128];
    wchar_t cl_anglespeedkey[128];
    wchar_t m_yaw[128];
    bool enabled;
    int current;
    int selected;
    std::optional<WINDOWPLACEMENT> placement;

    Config(const wchar_t *path, const wchar_t *section)
    {
        developer = (GetPrivateProfileIntW(section, L"developer", 0, path) == 1);
        bind_left = GetPrivateProfileIntW(section, L"bind_left", VK_LBUTTON, path);
        bind_right = GetPrivateProfileIntW(section, L"bind_right", VK_RBUTTON, path);
        bind_speed = GetPrivateProfileIntW(section, L"bind_speed", VK_LSHIFT, path);
        bind_cycle = GetPrivateProfileIntW(section, L"bind_cycle", VK_XBUTTON1, path);
        GetPrivateProfileStringW(section, L"rate", L"1000", rate, std::size(rate), path);
        GetPrivateProfileStringW(section, L"sleep", L"3500", sleep, std::size(sleep), path);
        GetPrivateProfileStringW(section, L"cl_yawspeed", L"75 120 210", cl_yawspeed, std::size(cl_yawspeed), path);
        GetPrivateProfileStringW(section, L"sensitivity", L"1.0", sensitivity, std::size(sensitivity), path);
        GetPrivateProfileStringW(section, L"cl_anglespeedkey", L"0.67", cl_anglespeedkey, std::size(cl_anglespeedkey), path);
        GetPrivateProfileStringW(section, L"m_yaw", L"0.022", m_yaw, std::size(m_yaw), path);
        enabled = (GetPrivateProfileIntW(section, L"enabled", 1, path) == 1);
        current = GetPrivateProfileIntW(section, L"current", 0, path);
        selected = GetPrivateProfileIntW(section, L"selected", 0, path);

        WINDOWPLACEMENT placement_;
        if (GetPrivateProfileStructW(section, L"placement", &placement_, sizeof(placement_), path)) {
            placement = placement_;
        }
    }

    void save(const wchar_t *path, const wchar_t *section)
    {
        using common::win32::WritePrivateProfileIntW;
        WritePrivateProfileIntW(section, L"developer", developer, path);
        WritePrivateProfileIntW(section, L"bind_left", bind_left, path);
        WritePrivateProfileIntW(section, L"bind_right", bind_right, path);
        WritePrivateProfileIntW(section, L"bind_speed", bind_speed, path);
        WritePrivateProfileIntW(section, L"bind_cycle", bind_cycle, path);
        WritePrivateProfileStringW(section, L"rate", rate, path);
        WritePrivateProfileStringW(section, L"sleep", sleep, path);
        WritePrivateProfileStringW(section, L"cl_yawspeed", cl_yawspeed, path);
        WritePrivateProfileStringW(section, L"sensitivity", sensitivity, path);
        WritePrivateProfileStringW(section, L"cl_anglespeedkey", cl_anglespeedkey, path);
        WritePrivateProfileStringW(section, L"m_yaw", m_yaw, path);
        WritePrivateProfileIntW(section, L"enabled", enabled, path);
        WritePrivateProfileIntW(section, L"current", current, path);
        WritePrivateProfileIntW(section, L"selected", selected, path);
        WritePrivateProfileStructW(section, L"placement", &(*placement), sizeof(*placement), path);
    }
};
