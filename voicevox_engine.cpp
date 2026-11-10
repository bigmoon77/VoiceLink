#include "voicevox_engine.h"

#include <winrt/Windows.Data.Json.h>
#include <winrt/base.h>

#include <stdexcept>
#include <utility>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "windowsapp.lib")

namespace
{
    constexpr wchar_t engine_host[] = L"127.0.0.1";
    constexpr INTERNET_PORT engine_port = 50021;

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
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0
        );

        if (length <= 0)
        {
            throw std::runtime_error("Invalid UTF-8 response");
        }

        std::wstring result(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            length
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
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0,
            nullptr,
            nullptr
        );

        if (length <= 0)
        {
            throw std::runtime_error("Invalid UTF-16 input");
        }

        std::string result(static_cast<size_t>(length), '\0');
        WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            length,
            nullptr,
            nullptr
        );
        return result;
    }

    std::string url_encode(const std::string& value)
    {
        constexpr char hex_digits[] = "0123456789ABCDEF";
        std::string encoded;
        encoded.reserve(value.size() * 3);

        for (unsigned char character : value)
        {
            const bool unreserved =
                (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') ||
                character == '-' || character == '_' ||
                character == '.' || character == '~';

            if (unreserved)
            {
                encoded.push_back(static_cast<char>(character));
            }
            else
            {
                encoded.push_back('%');
                encoded.push_back(hex_digits[character >> 4]);
                encoded.push_back(hex_digits[character & 0x0f]);
            }
        }

        return encoded;
    }

    bool is_wav_data(const std::vector<std::uint8_t>& data)
    {
        return data.size() >= 12 &&
            data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
            data[8] == 'W' && data[9] == 'A' && data[10] == 'V' && data[11] == 'E';
    }
}

voicevox_engine::~voicevox_engine()
{
    close_connection();
}

bool voicevox_engine::initialize(
    std::vector<tts_engine::speaker_style>& speakers,
    std::wstring& error
)
{
    if (!ensure_connection(error))
    {
        return false;
    }

    std::vector<std::uint8_t> response;
    if (!request(L"GET", L"/speakers", {}, nullptr, response, error))
    {
        return false;
    }

    try
    {
        const std::string json_text(response.begin(), response.end());
        const auto json_array = winrt::Windows::Data::Json::JsonArray::Parse(
            winrt::hstring(utf8_to_wide(json_text))
        );

        std::vector<tts_engine::speaker_style> result;
        for (const auto& speaker_value : json_array)
        {
            const auto speaker = speaker_value.GetObject();
            const std::wstring speaker_name = speaker.GetNamedString(L"name").c_str();
            const auto styles = speaker.GetNamedArray(L"styles");

            for (const auto& style_value : styles)
            {
                const auto style = style_value.GetObject();
                const auto style_id = static_cast<int>(style.GetNamedNumber(L"id", -1));
                const std::wstring style_name = style.GetNamedString(L"name").c_str();

                if (style_id < 0)
                {
                    continue;
                }

                result.push_back({ speaker_name, style_name, style_id });
            }
        }

        if (result.empty())
        {
            error = L"VOICEVOX Engine から話者・スタイルを取得できませんでした。";
            return false;
        }

        speakers = std::move(result);
        return true;
    }
    catch (const winrt::hresult_error& exception)
    {
        error = L"話者一覧の JSON 解析に失敗しました: ";
        error += exception.message().c_str();
        return false;
    }
    catch (const std::exception&)
    {
        error = L"話者一覧の文字コードまたは JSON 形式が不正です。";
        return false;
    }
}

bool voicevox_engine::synthesize(
    const std::wstring& text,
    int speaker_id,
    std::vector<std::uint8_t>& wav_data,
    std::wstring& error
)
{
    if (!_connection)
    {
        error = L"VOICEVOX Engine に接続されていません。";
        return false;
    }

    if (text.empty() || speaker_id < 0)
    {
        error = L"読み上げテキストまたは話者 ID が不正です。";
        return false;
    }

    std::string encoded_text;
    try
    {
        encoded_text = url_encode(wide_to_utf8(text));
    }
    catch (const std::exception&)
    {
        error = L"読み上げテキストを UTF-8 に変換できません。";
        return false;
    }

    const std::wstring query_path =
        L"/audio_query?text=" + utf8_to_wide(encoded_text) +
        L"&speaker=" + std::to_wstring(speaker_id);

    std::vector<std::uint8_t> query_response;
    if (!request(L"POST", query_path, {}, nullptr, query_response, error))
    {
        return false;
    }

    const std::string query_json(query_response.begin(), query_response.end());
    std::vector<std::uint8_t> synthesis_response;

    const std::wstring synthesis_path =
        L"/synthesis?speaker=" + std::to_wstring(speaker_id);

    if (!request(
        L"POST",
        synthesis_path,
        query_json,
        L"application/json; charset=utf-8",
        synthesis_response,
        error
    ))
    {
        return false;
    }

    if (!is_wav_data(synthesis_response))
    {
        error = L"VOICEVOX Engine から不正な WAV データが返されました。";
        return false;
    }

    wav_data = std::move(synthesis_response);
    return true;
}

bool voicevox_engine::ensure_connection(std::wstring& error)
{
    if (_session && _connection)
    {
        return true;
    }

    close_connection();

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

    WinHttpSetTimeouts(_session, 3000, 3000, 10000, 30000);

    _connection = WinHttpConnect(_session, engine_host, engine_port, 0);
    if (!_connection)
    {
        error = L"VOICEVOX Engine への接続を初期化できません: ";
        error += get_windows_error(GetLastError());
        close_connection();
        return false;
    }

    return true;
}

bool voicevox_engine::request(
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
        error = L"VOICEVOX Engine に接続されていません。";
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
        error = L"VOICEVOX Engine への HTTP 通信に失敗しました: ";
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

        const size_t old_size = response_data.size();
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
        error = L"VOICEVOX Engine が HTTP ";
        error += std::to_wstring(status_code);
        error += L" を返しました: ";

        if (!response_data.empty())
        {
            try
            {
                error += utf8_to_wide(std::string(response_data.begin(), response_data.end()));
            }
            catch (const std::exception&)
            {
                error += L"応答本文を読み取れません。";
            }
        }

        return false;
    }

    response = std::move(response_data);
    return true;
}

void voicevox_engine::close_connection()
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
