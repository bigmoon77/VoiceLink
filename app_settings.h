#pragma once

#include <cstdint>
#include <string>

class app_settings
{
public:
    enum class tts_engine_type
    {
        voicevox,
        gpt_sovits
    };

    struct gpt_sovits_settings
    {
        std::wstring host = L"127.0.0.1";
        std::uint16_t port = 9880;
        std::wstring reference_audio_path;
        std::wstring prompt_text;
        std::wstring prompt_language = L"ja";
        std::wstring text_language = L"ja";
    };

    bool load();
    bool save() const;

    const std::wstring& get_voicevox_path() const;
    void set_voicevox_path(const std::wstring& path);

    tts_engine_type get_tts_engine_type() const;
    void set_tts_engine_type(tts_engine_type value);

    const gpt_sovits_settings& get_gpt_sovits_settings() const;
    void set_gpt_sovits_settings(const gpt_sovits_settings& value);

private:
    std::wstring get_config_path() const;

    std::wstring _voicevox_path;
    tts_engine_type _tts_engine_type = tts_engine_type::voicevox;
    gpt_sovits_settings _gpt_sovits_settings;
};
