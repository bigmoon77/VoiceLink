#pragma once

#include <cstdint>
#include <string>
#include <vector>

class tts_engine
{
public:
    struct speaker_style
    {
        std::wstring speaker_name;
        std::wstring style_name;
        int speaker_id = -1;
    };

    virtual ~tts_engine() = default;

    virtual bool initialize(
        std::vector<speaker_style>& speakers,
        std::wstring& error
    ) = 0;

    virtual bool synthesize(
        const std::wstring& text,
        int speaker_id,
        std::vector<std::uint8_t>& wav_data,
        std::wstring& error
    ) = 0;
};
