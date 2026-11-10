#pragma once

#include "audio_player.h"
#include "main_window.h"
#include "voicevox_engine.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

class tts_controller
{
public:
    tts_controller(main_window& window, audio_player& player);
    ~tts_controller();

    tts_controller(const tts_controller&) = delete;
    tts_controller& operator=(const tts_controller&) = delete;

    void initialize_async();

private:
    static constexpr UINT speakers_ready_message = WM_APP + 1;
    static constexpr UINT synthesis_ready_message = WM_APP + 2;

    struct speakers_result
    {
        std::vector<main_window::speaker_item> speakers;
        std::wstring error;
    };

    struct synthesis_result
    {
        std::vector<std::uint8_t> wav_data;
        std::wstring error;
    };

    void on_play();
    bool handle_window_message(UINT message, WPARAM w_param, LPARAM l_param);
    void start_worker(std::function<void()> task);

    main_window& _window;
    audio_player& _player;
    voicevox_engine _engine;
    std::thread _worker;
    std::atomic<bool> _busy{ false };
    bool _initialized = false;
};
