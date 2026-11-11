#pragma once

#include "audio_player.h"
#include "../main_window.h"
#include "tts_engine.h"
#include "voicevox_process.h"
#include "gpt_sovits_engine.h"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
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
    void set_voicevox_path(const std::wstring& path);
    void set_engine_type(bool use_gpt_sovits);
    void set_gpt_sovits_settings(const gpt_sovits_engine::settings& value);

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
    void on_volume_test();
    bool handle_window_message(UINT message, WPARAM w_param, LPARAM l_param);
    void start_worker(std::function<void()> task);

    main_window& _window;
    audio_player& _player;
    std::unique_ptr<tts_engine> _engine;
    voicevox_process _voicevox_process;
    std::wstring _voicevox_path;
    bool _use_gpt_sovits = false;
    gpt_sovits_engine::settings _gpt_sovits_settings;
    std::thread _worker;
    std::atomic<bool> _busy{ false };
    std::mutex _result_mutex;
    speakers_result _speakers_result;
    synthesis_result _synthesis_result;
    bool _initialized = false;
};
