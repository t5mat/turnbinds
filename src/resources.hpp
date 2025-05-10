#pragma once

#include "common.hpp"

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

inline auto load_icon_resource(HINSTANCE instance)
{
    return static_cast<HICON>(::LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR));
}
