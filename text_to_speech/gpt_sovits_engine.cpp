#include "gpt_sovits_engine.h"

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/base.h>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "windowsapp.lib")

namespace
{
    constexpr std::size_t maximum_response_size = 256ull * 1024ull * 1024ull;

    std::wstring get_windows_error(DWORD error_code)
    {
        wchar_t* message_buffer = nullptr;
        const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            error_code,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPWSTR>(&message_buffer),
            0,
            nullptr
        );

        std::wstring message = length ? message_buffer : L"Unknown Windows error";
        if (message_buffer)
        {
            LocalFree(message_buffer);
        }

        while (!message.empty() &&
            (message.back() == L'\r' || message.back() == L'\n' || message.back() == L' '))
        {
            message.pop_back();
        }

        return message;
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
            throw std::runtime_error("Invalid UTF-8 response");
        }

        std::wstring result(static_cast<std::size_t>(length), L'\0');
        if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), length
        ) != length)
        {
            throw std::runtime_error("UTF-8 conversion failed");
        }
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
            throw std::runtime_error("Invalid UTF-16 input");
        }

        std::string result(static_cast<std::size_t>(length), '\0');
        if (WideCharToMultiByte(
            CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), length, nullptr, nullptr
        ) != length)
        {
            throw std::runtime_error("UTF-16 conversion failed");
        }
        return result;
    }

    bool is_wav_data(const std::vector<std::uint8_t>& data)
    {
        return data.size() >= 12 &&
            data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
            data[8] == 'W' && data[9] == 'A' && data[10] == 'V' && data[11] == 'E';
    }
}

gpt_sovits_engine::~gpt_sovits_engine()
{
    close_connection();
}

void gpt_sovits_engine::set_settings(const settings& value)
{
    if (_settings.host != value.host || _settings.port != value.port)
    {
        close_connection();
    }
    _settings = value;
}

bool gpt_sovits_engine::initialize(
    std::vector<tts_engine::speaker_style>& speakers,
    std::wstring& error
)
{
    if (_settings.reference_audio_path.empty())
    {
        error = L"GPT-SoVITS の参照音声ファイルが設定されていません。";
        return false;
    }
    if (_settings.prompt_text.empty())
    {
        error = L"GPT-SoVITS の参照音声テキストが設定されていません。";
        return false;
    }

    const DWORD audio_attributes = GetFileAttributesW(_settings.reference_audio_path.c_str());
    if (audio_attributes == INVALID_FILE_ATTRIBUTES ||
        (audio_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        error = L"GPT-SoVITS の参照音声ファイルが見つかりません。";
        return false;
    }

    if (!ensure_connection(error))
    {
        return false;
    }

    std::vector<std::uint8_t> response;
    if (!request(L"GET", L"/openapi.json", {}, nullptr, response, error))
    {
        return false;
    }

    speakers.clear();
    speakers.push_back({ L"GPT-SoVITS", L"Reference voice", 0 });
    return true;
}

bool gpt_sovits_engine::synthesize(
    const std::wstring& text,
    int speaker_id,
    std::vector<std::uint8_t>& wav_data,
    std::wstring& error
)
{
    if (speaker_id != 0)
    {
        error = L"GPT-SoVITS の話者 ID が不正です。";
        return false;
    }
    if (text.empty())
    {
        error = L"合成するテキストを入力してください。";
        return false;
    }
    if (_settings.reference_audio_path.empty())
    {
        error = L"GPT-SoVITS の参照音声ファイルが設定されていません。";
        return false;
    }
    if (_settings.prompt_text.empty())
    {
        error = L"GPT-SoVITS の参照音声テキストが設定されていません。";
        return false;
    }
    if (!ensure_connection(error))
    {
        return false;
    }

    try
    {
        winrt::Windows::Data::Json::JsonObject request_json;
        request_json.Insert(
            L"text",
            winrt::Windows::Data::Json::JsonValue::CreateStringValue(winrt::hstring(text))
        );
        request_json.Insert(
            L"text_lang",
            winrt::Windows::Data::Json::JsonValue::CreateStringValue(winrt::hstring(_settings.text_language))
        );
        request_json.Insert(
            L"ref_audio_path",
            winrt::Windows::Data::Json::JsonValue::CreateStringValue(winrt::hstring(_settings.reference_audio_path))
        );
        request_json.Insert(
            L"prompt_text",
            winrt::Windows::Data::Json::JsonValue::CreateStringValue(winrt::hstring(_settings.prompt_text))
        );
        request_json.Insert(
            L"prompt_lang",
            winrt::Windows::Data::Json::JsonValue::CreateStringValue(winrt::hstring(_settings.prompt_language))
        );
        request_json.Insert(L"media_type", winrt::Windows::Data::Json::JsonValue::CreateStringValue(L"wav"));
        request_json.Insert(L"streaming_mode", winrt::Windows::Data::Json::JsonValue::CreateBooleanValue(false));
        request_json.Insert(L"batch_size", winrt::Windows::Data::Json::JsonValue::CreateNumberValue(1));
        request_json.Insert(L"speed_factor", winrt::Windows::Data::Json::JsonValue::CreateNumberValue(1.0));

        const std::string body = wide_to_utf8(request_json.Stringify().c_str());
        std::vector<std::uint8_t> response;
        if (!request(
            L"POST",
            L"/tts",
            body,
            L"application/json; charset=utf-8",
            response,
            error
        ))
        {
            return false;
        }

        if (!is_wav_data(response))
        {
            error = L"GPT-SoVITS から不正な WAV データが返されました。";
            return false;
        }

        wav_data = std::move(response);
        return true;
    }
    catch (const winrt::hresult_error& exception)
    {
        error = L"GPT-SoVITS のリクエスト JSON を作成できません: ";
        error += exception.message().c_str();
        return false;
    }
    catch (const std::exception&)
    {
        error = L"GPT-SoVITS のリクエスト文字列を UTF-8 に変換できません。";
        return false;
    }
}

bool gpt_sovits_engine::ensure_connection(std::wstring& error)
{
    if (_session && _connection)
    {
        return true;
    }

    close_connection();

    if (_settings.host.empty() || _settings.port == 0)
    {
        error = L"GPT-SoVITS の接続先が不正です。";
        return false;
    }

    _session = WinHttpOpen(
        L"VoiceLink/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!_session)
    {
        error = L"WinHTTP セッションを作成できません: ";
        error += get_windows_error(GetLastError());
        return false;
    }

    WinHttpSetTimeouts(_session, 3000, 3000, 10000, 300000);
    _connection = WinHttpConnect(_session, _settings.host.c_str(), _settings.port, 0);
    if (!_connection)
    {
        error = L"GPT-SoVITS API への接続を初期化できません: ";
        error += get_windows_error(GetLastError());
        close_connection();
        return false;
    }

    return true;
}

bool gpt_sovits_engine::request(
    const wchar_t* method,
    const std::wstring& path,
    const std::string& body,
    const wchar_t* content_type,
    std::vector<std::uint8_t>& response,
    std::wstring& error
)
{
    if (!_connection)
    {
        error = L"GPT-SoVITS API に接続されていません。";
        return false;
    }

    HINTERNET request_handle = WinHttpOpenRequest(
        _connection,
        method,
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        0
    );
    if (!request_handle)
    {
        error = L"HTTP リクエストを作成できません: ";
        error += get_windows_error(GetLastError());
        return false;
    }

    std::wstring headers;
    if (content_type)
    {
        headers = L"Content-Type: ";
        headers += content_type;
        headers += L"\r\n";
    }

    const BOOL sent = WinHttpSendRequest(
        request_handle,
        headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
        headers.empty() ? 0 : static_cast<DWORD>(headers.size()),
        body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()),
        static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()),
        0
    );
    if (!sent || !WinHttpReceiveResponse(request_handle, nullptr))
    {
        error = L"GPT-SoVITS API への HTTP 通信に失敗しました: ";
        error += get_windows_error(GetLastError());
        WinHttpCloseHandle(request_handle);
        return false;
    }

    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    if (!WinHttpQueryHeaders(
        request_handle,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status_code,
        &status_size,
        WINHTTP_NO_HEADER_INDEX
    ))
    {
        error = L"HTTP ステータスコードを取得できません: ";
        error += get_windows_error(GetLastError());
        WinHttpCloseHandle(request_handle);
        return false;
    }

    std::vector<std::uint8_t> response_data;
    for (;;)
    {
        DWORD available_size = 0;
        if (!WinHttpQueryDataAvailable(request_handle, &available_size))
        {
            error = L"HTTP 応答サイズを取得できません: ";
            error += get_windows_error(GetLastError());
            WinHttpCloseHandle(request_handle);
            return false;
        }
        if (available_size == 0)
        {
            break;
        }
        if (available_size > maximum_response_size ||
            response_data.size() > maximum_response_size - available_size)
        {
            error = L"GPT-SoVITS API の応答サイズが上限を超えています。";
            WinHttpCloseHandle(request_handle);
            return false;
        }

        const std::size_t old_size = response_data.size();
        response_data.resize(old_size + available_size);
        DWORD downloaded_size = 0;
        if (!WinHttpReadData(
            request_handle,
            response_data.data() + old_size,
            available_size,
            &downloaded_size
        ))
        {
            error = L"HTTP 応答を読み込めません: ";
            error += get_windows_error(GetLastError());
            WinHttpCloseHandle(request_handle);
            return false;
        }
        response_data.resize(old_size + downloaded_size);
    }

    WinHttpCloseHandle(request_handle);

    if (status_code < 200 || status_code >= 300)
    {
        error = L"GPT-SoVITS API が HTTP ";
        error += std::to_wstring(status_code);
        error += L" を返しました: ";
        try
        {
            error += utf8_to_wide(std::string(response_data.begin(), response_data.end()));
        }
        catch (const std::exception&)
        {
            error += L"応答本文を読み取れません。";
        }
        return false;
    }

    response = std::move(response_data);
    return true;
}

void gpt_sovits_engine::close_connection()
{
    if (_connection)
    {
        WinHttpCloseHandle(_connection);
        _connection = nullptr;
    }
    if (_session)
    {
        WinHttpCloseHandle(_session);
        _session = nullptr;
    }
}
