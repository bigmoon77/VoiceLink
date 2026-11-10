#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <string>

class voicevox_process
{
public:
    voicevox_process() = default;
    ~voicevox_process();

    voicevox_process(const voicevox_process&) = delete;
    voicevox_process& operator=(const voicevox_process&) = delete;

    bool start(std::wstring& error);

private:
    static bool is_engine_ready();
    static bool find_engine_path(std::wstring& path);
    static std::wstring get_environment_path(const wchar_t* name);

    HANDLE _job = nullptr;
    PROCESS_INFORMATION _process_info{};
};
