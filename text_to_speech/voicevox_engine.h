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

class voicevox_engine : public tts_engine
{
public:
    voicevox_engine() = default;
    ~voicevox_engine();

    voicevox_engine(const voicevox_engine&) = delete;
    voicevox_engine& operator=(const voicevox_engine&) = delete;

    bool initialize(std::vector<tts_engine::speaker_style>& speakers, std::wstring& error) override;
    bool synthesize(
        const std::wstring& text,
        int speaker_id,
        std::vector<std::uint8_t>& wav_data,
        std::wstring& error
    ) override;

private:
    bool request(
        const wchar_t* method,
        const std::wstring& path,
        const std::string& body,
        const wchar_t* content_type,
        std::vector<std::uint8_t>& response,
        std::wstring& error
    );

    bool ensure_connection(std::wstring& error);
    void close_connection();

    HINTERNET _session = nullptr;
    HINTERNET _connection = nullptr;
};
