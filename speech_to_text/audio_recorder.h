#pragma once

#include <Windows.h>
#include <mmsystem.h>

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class audio_recorder
{
public:
    audio_recorder();
    ~audio_recorder();

    audio_recorder(const audio_recorder&) = delete;
    audio_recorder& operator=(const audio_recorder&) = delete;

    bool start(std::wstring& error);
    bool stop(std::vector<std::uint8_t>& wav_data, std::wstring& error);
    bool is_recording() const;

private:
    static constexpr DWORD sample_rate = 16000;
    static constexpr WORD channel_count = 1;
    static constexpr WORD bits_per_sample = 16;
    static constexpr std::size_t buffer_size = 4096;
    static constexpr std::size_t buffer_count = 4;

    static void CALLBACK wave_in_callback(
        HWAVEIN wave_in,
        UINT message,
        DWORD_PTR instance,
        DWORD_PTR parameter_1,
        DWORD_PTR parameter_2
    );

    void on_buffer_done(WAVEHDR* header);
    void release_buffers();
    static void append_u16(std::vector<std::uint8_t>& data, WORD value);
    static void append_u32(std::vector<std::uint8_t>& data, DWORD value);

    mutable std::mutex _mutex;
    HWAVEIN _wave_in = nullptr;
    std::array<WAVEHDR, buffer_count> _headers{};
    std::array<std::array<char, buffer_size>, buffer_count> _buffers{};
    std::vector<std::uint8_t> _recorded_data;
    bool _recording = false;
};
