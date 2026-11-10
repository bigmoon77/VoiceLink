#include "app_settings.h"

#include <Windows.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/base.h>

#include <string>
#include <vector>

#pragma comment(lib, "windowsapp.lib")

namespace
{
    std::wstring get_app_data_path()
    {
        const DWORD required = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
        if (required == 0)
        {
            return {};
        }

        std::wstring path(required, L'\0');
        const DWORD copied = GetEnvironmentVariableW(L"APPDATA", path.data(), required);
        if (copied == 0 || copied >= required)
        {
            return {};
        }

        path.resize(copied);
        return path;
    }

    std::wstring utf8_to_wide(const std::string& value)
    {
        if (value.empty())
        {
            return {};
        }

        const int length = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), nullptr, 0
        );
        if (length <= 0)
        {
            return {};
        }

        std::wstring result(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), length
        );
        return result;
    }

    std::string wide_to_utf8(const std::wstring& value)
    {
        if (value.empty())
        {
            return {};
        }

        const int length = WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr
        );
        if (length <= 0)
        {
            return {};
        }

        std::string result(static_cast<size_t>(length), '\0');
        WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), length, nullptr, nullptr
        );
        return result;
    }
}

bool app_settings::load()
{
    const std::wstring config_path = get_config_path();
    if (config_path.empty())
    {
        return false;
    }

    HANDLE file = CreateFileW(
        config_path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (file == INVALID_HANDLE_VALUE)
    {
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    }

    LARGE_INTEGER file_size{};
    if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart <= 0 ||
        file_size.QuadPart > 1024 * 1024)
    {
        CloseHandle(file);
        return false;
    }

    std::string json(static_cast<size_t>(file_size.QuadPart), '\0');
    DWORD bytes_read = 0;
    const BOOL read_ok = ReadFile(
        file, json.data(), static_cast<DWORD>(json.size()), &bytes_read, nullptr
    );
    CloseHandle(file);

    if (!read_ok || bytes_read != json.size())
    {
        return false;
    }

    try
    {
        const auto object = winrt::Windows::Data::Json::JsonObject::Parse(
            winrt::hstring(utf8_to_wide(json))
        );
        const auto value = object.GetNamedString(L"voicevox_path", L"");
        _voicevox_path = value.c_str();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool app_settings::save() const
{
    const std::wstring config_path = get_config_path();
    if (config_path.empty())
    {
        return false;
    }

    try
    {
        winrt::Windows::Data::Json::JsonObject object;
        object.Insert(
            L"voicevox_path",
            winrt::Windows::Data::Json::JsonValue::CreateStringValue(winrt::hstring(_voicevox_path))
        );

        const std::string json = wide_to_utf8(
            object.Stringify().c_str()
        );
        if (json.empty())
        {
            return false;
        }

        HANDLE file = CreateFileW(
            config_path.c_str(), GENERIC_WRITE, 0,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
        );
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        DWORD bytes_written = 0;
        const BOOL write_ok = WriteFile(
            file, json.data(), static_cast<DWORD>(json.size()),
            &bytes_written, nullptr
        );
        CloseHandle(file);
        return write_ok && bytes_written == json.size();
    }
    catch (...)
    {
        return false;
    }
}

const std::wstring& app_settings::get_voicevox_path() const
{
    return _voicevox_path;
}

void app_settings::set_voicevox_path(const std::wstring& path)
{
    _voicevox_path = path;
}

std::wstring app_settings::get_config_path() const
{
    const std::wstring app_data = get_app_data_path();
    if (app_data.empty())
    {
        return {};
    }

    const std::wstring directory = app_data + L"\\VoiceLink";
    if (!CreateDirectoryW(directory.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
    {
        return {};
    }

    return directory + L"\\config.json";
}
