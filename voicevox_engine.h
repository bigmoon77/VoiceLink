#pragma once

#include <Windows.h>
#include <winhttp.h>

#include <cstdint>
#include <string>
#include <vector>

class voicevox_engine
{
public:
    struct speaker_style
    {
        std::wstring speaker_name;
        std::wstring style_name;
        int speaker_id = -1;
    };

    voicevox_engine() = default;
    ~voicevox_engine();

    voicevox_engine(const voicevox_engine&) = delete;
    voicevox_engine& operator=(const voicevox_engine&) = delete;

    bool initialize(std::vector<speaker_style>& speakers, std::wstring& error);
    bool synthesize(
        const std::wstring& text,
        int speaker_id,
        std::vector<std::uint8_t>& wav_data,
        std::wstring& error
    );

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
