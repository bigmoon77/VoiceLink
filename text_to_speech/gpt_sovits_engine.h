#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "tts_engine.h"

#include <Windows.h>
#include <winhttp.h>

#include <cstdint>
#include <string>
#include <vector>

class gpt_sovits_engine : public tts_engine
{
public:
    struct settings
    {
        std::wstring host = L"127.0.0.1";
        INTERNET_PORT port = 9880;
        std::wstring reference_audio_path;
        std::wstring prompt_text;
        std::wstring prompt_language = L"ja";
        std::wstring text_language = L"ja";
    };

    gpt_sovits_engine() = default;
    ~gpt_sovits_engine() override;

    gpt_sovits_engine(const gpt_sovits_engine&) = delete;
    gpt_sovits_engine& operator=(const gpt_sovits_engine&) = delete;

    void set_settings(const settings& value);

    bool initialize(
        std::vector<tts_engine::speaker_style>& speakers,
        std::wstring& error
    ) override;

    bool synthesize(
        const std::wstring& text,
        int speaker_id,
        std::vector<std::uint8_t>& wav_data,
        std::wstring& error
    ) override;

private:
    bool ensure_connection(std::wstring& error);
    bool request(
        const wchar_t* method,
        const std::wstring& path,
        const std::string& body,
        const wchar_t* content_type,
        std::vector<std::uint8_t>& response,
        std::wstring& error
    );
    void close_connection();

    settings _settings;
    HINTERNET _session = nullptr;
    HINTERNET _connection = nullptr;
};
