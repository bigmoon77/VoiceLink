#pragma once

#include <Windows.h>
#include <mmsystem.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

class audio_recorder
{
public:
    static constexpr UINT limit_reached_message = WM_APP + 21;
    static constexpr int sample_rate = 16000;
    static constexpr int max_duration_seconds = 25;

    audio_recorder() = default;
    ~audio_recorder();

    audio_recorder(const audio_recorder&) = delete;
    audio_recorder& operator=(const audio_recorder&) = delete;

    bool start(HWND callback_window, std::wstring& error);
    bool stop(std::wstring& error);
    bool handle_message(UINT message, WPARAM w_param, LPARAM l_param);

    bool is_recording() const;
    const std::vector<float>& get_samples() const;

private:
    static constexpr size_t buffer_count = 4;
    static constexpr size_t buffer_sample_count = sample_rate / 4;
    static constexpr size_t max_sample_count =
        static_cast<size_t>(sample_rate) * max_duration_seconds;

    void release_device();
    static std::wstring get_wave_error(MMRESULT result);

    HWAVEIN _wave_in = nullptr;
    HWND _callback_window = nullptr;
    WAVEFORMATEX _format{};
    std::array<std::vector<std::int16_t>, buffer_count> _buffers;
    std::array<WAVEHDR, buffer_count> _headers{};
    size_t _prepared_count = 0;
    std::vector<float> _samples;
    bool _recording = false;
};
