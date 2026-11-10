#include "voicevox_process.h"

#include <Windows.h>
#include <winhttp.h>

#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace
{
    constexpr INTERNET_PORT engine_port = 50021;
    constexpr DWORD startup_timeout_ms = 90000;
    constexpr DWORD startup_poll_interval_ms = 500;

    bool check_engine_api()
    {
        HINTERNET session = WinHttpOpen(
            L"VoiceLink/1.0",
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0
        );
        if (!session)
        {
            return false;
        }

        WinHttpSetTimeouts(session, 300, 300, 500, 500);
        HINTERNET connection = WinHttpConnect(session, L"127.0.0.1", engine_port, 0);
        if (!connection)
        {
            WinHttpCloseHandle(session);
            return false;
        }

        HINTERNET request = WinHttpOpenRequest(
            connection, L"GET", L"/version", nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0
        );

        bool ready = false;
        if (request)
        {
            if (WinHttpSendRequest(
                request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0
            ) && WinHttpReceiveResponse(request, nullptr))
            {
                DWORD status = 0;
                DWORD size = sizeof(status);
                ready = WinHttpQueryHeaders(
                    request,
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX,
                    &status,
                    &size,
                    WINHTTP_NO_HEADER_INDEX
                ) && status >= 200 && status < 300;
            }
            WinHttpCloseHandle(request);
        }

        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return ready;
    }

    bool add_engine_candidate(
        const std::wstring& candidate,
        std::wstring& result
    )
    {
        if (is_file(candidate))
        {
            result = candidate;
            return true;
        }
        return false;
    }
}

voicevox_process::~voicevox_process()
{
    if (_job)
    {
        // Closing the job terminates only processes started and assigned by VoiceLink.
        CloseHandle(_job);
        _job = nullptr;
    }

    if (_process_info.hProcess)
    {
        CloseHandle(_process_info.hProcess);
        _process_info.hProcess = nullptr;
    }

    if (_process_info.hThread)
    {
        CloseHandle(_process_info.hThread);
        _process_info.hThread = nullptr;
    }
}

bool voicevox_process::start(const std::wstring& configured_path, std::wstring& error)
{
    if (_job && _process_info.hProcess &&
        WaitForSingleObject(_process_info.hProcess, 0) == WAIT_TIMEOUT)
    {
        return true;
    }

    // Reuse an Engine started by another application.
    if (is_engine_ready())
    {
        return true;
    }

    std::wstring executable_path = configured_path;
    if (!executable_path.empty())
    {
        if (!is_file(executable_path))
        {
            error = L"指定された VOICEVOX の実行ファイルが見つかりません: ";
            error += executable_path;
            return false;
        }
    }
    else if (!find_engine_path(executable_path))
    {
        error =
            L"VOICEVOX Engine の run.exe が見つかりません。"
            L" パスを指定するか、VOICEVOX のインストール状態を確認してください。";
        return false;
    }

    _job = CreateJobObjectW(nullptr, nullptr);
    if (!_job)
    {
        error = L"VOICEVOX 用 Job Object を作成できません。";
        return false;
    }

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(
        _job,
        JobObjectExtendedLimitInformation,
        &limits,
        sizeof(limits)
    ))
    {
        error = L"VOICEVOX の終了管理を設定できません。";
        CloseHandle(_job);
        _job = nullptr;
        return false;
    }

    std::wstring command_line = L"\"" + executable_path + L"\"";
    const size_t separator = executable_path.find_last_of(L"\\/");
    const std::wstring working_directory = separator == std::wstring::npos
        ? std::wstring()
        : executable_path.substr(0, separator);
    STARTUPINFOW startup_info{};
    startup_info.cb = sizeof(startup_info);

    if (!CreateProcessW(
        executable_path.c_str(),
        command_line.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_SUSPENDED | CREATE_NO_WINDOW,
        nullptr,
        working_directory.empty() ? nullptr : working_directory.c_str(),
        &startup_info,
        &_process_info
    ))
    {
        error = L"VOICEVOX Engine を起動できません。";
        CloseHandle(_job);
        _job = nullptr;
        return false;
    }

    if (!AssignProcessToJobObject(_job, _process_info.hProcess))
    {
        error = L"VOICEVOX Engine の終了管理に登録できません。";
        TerminateProcess(_process_info.hProcess, 1);
        CloseHandle(_process_info.hThread);
        CloseHandle(_process_info.hProcess);
        _process_info = {};
        CloseHandle(_job);
        _job = nullptr;
        return false;
    }

    ResumeThread(_process_info.hThread);
    CloseHandle(_process_info.hThread);
    _process_info.hThread = nullptr;

    const ULONGLONG start_time = GetTickCount64();
    while (GetTickCount64() - start_time < startup_timeout_ms)
    {
        if (WaitForSingleObject(_process_info.hProcess, 0) == WAIT_OBJECT_0)
        {
            error = L"VOICEVOX Engine が起動中に終了しました。";
            CloseHandle(_process_info.hProcess);
            _process_info.hProcess = nullptr;
            CloseHandle(_job);
            _job = nullptr;
            return false;
        }

        if (is_engine_ready())
        {
            return true;
        }

        Sleep(startup_poll_interval_ms);
    }

    error = L"VOICEVOX Engine の起動がタイムアウトしました。";
    CloseHandle(_job);
    _job = nullptr;
    CloseHandle(_process_info.hProcess);
    _process_info.hProcess = nullptr;
    return false;
}

bool voicevox_process::is_file(const std::wstring& path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool voicevox_process::is_engine_ready()
{
    return check_engine_api();
}

std::wstring voicevox_process::get_environment_path(const wchar_t* name)
{
    const DWORD length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0)
    {
        return {};
    }

    std::wstring value(length, L'\0');
    const DWORD copied = GetEnvironmentVariableW(name, value.data(), length);
    if (copied == 0 || copied >= length)
    {
        return {};
    }

    value.resize(copied);
    return value;
}

bool voicevox_process::find_engine_path(std::wstring& path)
{
    const std::wstring local_app_data = get_environment_path(L"LOCALAPPDATA");
    const std::wstring app_data = get_environment_path(L"APPDATA");

    if (!local_app_data.empty())
    {
        if (add_engine_candidate(
            local_app_data + L"\\Programs\\VOICEVOX\\vv-engine\\run.exe", path) ||
            add_engine_candidate(
            local_app_data + L"\\Programs\\voicevox\\vv-engine\\run.exe", path))
        {
            return true;
        }
    }

    if (!app_data.empty())
    {
        const std::wstring engines_path = app_data + L"\\voicevox\\vvpp-engines\\";
        WIN32_FIND_DATAW find_data{};
        HANDLE find = FindFirstFileW(
            (engines_path + L"VOICEVOX_Engine+*").c_str(),
            &find_data
        );

        if (find != INVALID_HANDLE_VALUE)
        {
            do
            {
                if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                {
                    continue;
                }

                const std::wstring candidate =
                    engines_path + find_data.cFileName + L"\\run.exe";
                if (is_file(candidate))
                {
                    path = candidate;
                    FindClose(find);
                    return true;
                }
            } while (FindNextFileW(find, &find_data));

            FindClose(find);
        }
    }

    return false;
}
