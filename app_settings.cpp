#include "app_settings.h"
#include "storage_system.h"

#include <Windows.h>
#include <winrt/Windows.Foundation.Collections.h>
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

    std::vector<std::uint8_t> file_data;
    if (!storage_system::read_binary(config_path, file_data))
    {
        return GetFileAttributesW(config_path.c_str()) == INVALID_FILE_ATTRIBUTES &&
            GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    if (file_data.empty() || file_data.size() > 1024 * 1024) return false;
    const std::string json(
        reinterpret_cast<const char*>(file_data.data()), file_data.size()
    );

    try
    {
        const auto object = winrt::Windows::Data::Json::JsonObject::Parse(
            winrt::hstring(utf8_to_wide(json))
        );
        const auto value = object.GetNamedString(L"voicevox_path", L"");
        _voicevox_path = value.c_str();

        const std::wstring engine_type = object.GetNamedString(
            L"tts_engine", L"voicevox"
        ).c_str();
        _tts_engine_type = engine_type == L"gpt_sovits"
            ? tts_engine_type::gpt_sovits
            : tts_engine_type::voicevox;

        _gpt_sovits_settings.host = object.GetNamedString(
            L"gpt_sovits_host", L"127.0.0.1"
        ).c_str();
        const INT32 port = object.GetNamedNumber(L"gpt_sovits_port", 9880);
        if (port > 0 && port <= 65535)
        {
            _gpt_sovits_settings.port = static_cast<std::uint16_t>(port);
        }
        _gpt_sovits_settings.reference_audio_path = object.GetNamedString(
            L"gpt_sovits_reference_audio", L""
        ).c_str();
        _gpt_sovits_settings.prompt_text = object.GetNamedString(
            L"gpt_sovits_prompt_text", L""
        ).c_str();
        _gpt_sovits_settings.prompt_language = object.GetNamedString(
            L"gpt_sovits_prompt_language", L"ja"
        ).c_str();
        _gpt_sovits_settings.text_language = object.GetNamedString(
            L"gpt_sovits_text_language", L"ja"
        ).c_str();
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
        object.Insert(
            L"tts_engine",
            winrt::Windows::Data::Json::JsonValue::CreateStringValue(
                _tts_engine_type == tts_engine_type::gpt_sovits ? L"gpt_sovits" : L"voicevox"
            )
        );
        object.Insert(
            L"gpt_sovits_host",
            winrt::Windows::Data::Json::JsonValue::CreateStringValue(
                winrt::hstring(_gpt_sovits_settings.host)
            )
        );
        object.Insert(
            L"gpt_sovits_port",
            winrt::Windows::Data::Json::JsonValue::CreateNumberValue(
                _gpt_sovits_settings.port
            )
        );
        object.Insert(
            L"gpt_sovits_reference_audio",
            winrt::Windows::Data::Json::JsonValue::CreateStringValue(
                winrt::hstring(_gpt_sovits_settings.reference_audio_path)
            )
        );
        object.Insert(
            L"gpt_sovits_prompt_text",
            winrt::Windows::Data::Json::JsonValue::CreateStringValue(
                winrt::hstring(_gpt_sovits_settings.prompt_text)
            )
        );
        object.Insert(
            L"gpt_sovits_prompt_language",
            winrt::Windows::Data::Json::JsonValue::CreateStringValue(
                winrt::hstring(_gpt_sovits_settings.prompt_language)
            )
        );
        object.Insert(
            L"gpt_sovits_text_language",
            winrt::Windows::Data::Json::JsonValue::CreateStringValue(
                winrt::hstring(_gpt_sovits_settings.text_language)
            )
        );

        const std::string json = wide_to_utf8(
            object.Stringify().c_str()
        );
        if (json.empty())
        {
            return false;
        }

        std::vector<std::uint8_t> file_data(json.begin(), json.end());
        return storage_system::write_binary_atomic(config_path, file_data);
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

app_settings::tts_engine_type app_settings::get_tts_engine_type() const
{
    return _tts_engine_type;
}

void app_settings::set_tts_engine_type(tts_engine_type value)
{
    _tts_engine_type = value;
}

const app_settings::gpt_sovits_settings& app_settings::get_gpt_sovits_settings() const
{
    return _gpt_sovits_settings;
}

void app_settings::set_gpt_sovits_settings(const gpt_sovits_settings& value)
{
    _gpt_sovits_settings = value;
}

std::wstring app_settings::get_config_path() const
{
    return storage_system::get_config_path(true);
}
