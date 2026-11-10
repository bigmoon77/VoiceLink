#include "tts_controller.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>

tts_controller::tts_controller(main_window& window, audio_player& player)
    : _window(window)
    , _player(player)
{
    _window.set_play_callback([this]()
    {
        on_play();
    });

    _window.set_message_callback([this](UINT message, WPARAM w_param, LPARAM l_param)
    {
        return handle_window_message(message, w_param, l_param);
    });

    _window.set_play_enabled(false);
}

tts_controller::~tts_controller()
{
    _window.set_play_callback({});
    _window.set_message_callback({});

    if (_worker.joinable())
    {
        _worker.join();
    }

    _player.stop();
}

void tts_controller::initialize_async()
{
    if (_busy.exchange(true))
    {
        return;
    }

    _window.set_status(L"VOICEVOX Engine に接続しています...");

    start_worker([this]()
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        auto* result = new speakers_result();
        std::vector<voicevox_engine::speaker_style> engine_speakers;

        if (_engine.initialize(engine_speakers, result->error))
        {
            result->speakers.reserve(engine_speakers.size());

            for (const auto& speaker : engine_speakers)
            {
                std::wstring display_name = speaker.speaker_name;
                display_name += L" / ";
                display_name += speaker.style_name;

                result->speakers.push_back({ std::move(display_name), speaker.speaker_id });
            }
        }

        winrt::uninit_apartment();

        if (!PostMessageW(_window.get_handle(), speakers_ready_message, 0, reinterpret_cast<LPARAM>(result)))
        {
            delete result;
            _busy = false;
        }
    });
}

void tts_controller::on_play()
{
    if (!_initialized || _busy.exchange(true))
    {
        return;
    }

    const std::wstring text = _window.get_text();
    int speaker_id = -1;

    if (text.empty())
    {
        _window.set_status(L"読み上げるテキストを入力してください。");
        _busy = false;
        return;
    }

    if (!_window.get_selected_speaker_id(speaker_id))
    {
        _window.set_status(L"話者・スタイルを選択してください。");
        _busy = false;
        return;
    }

    _window.set_play_enabled(false);
    _window.set_status(L"音声を合成しています...");

    start_worker([this, text, speaker_id]()
    {
        auto* result = new synthesis_result();

        _engine.synthesize(text, speaker_id, result->wav_data, result->error);

        if (!PostMessageW(_window.get_handle(), synthesis_ready_message, 0, reinterpret_cast<LPARAM>(result)))
        {
            delete result;
            _busy = false;
        }
    });
}

bool tts_controller::handle_window_message(UINT message, WPARAM, LPARAM l_param)
{
    if (message == speakers_ready_message)
    {
        if (_worker.joinable())
        {
            _worker.join();
        }

        std::unique_ptr<speakers_result> result(reinterpret_cast<speakers_result*>(l_param));
        _busy = false;

        if (!result->error.empty())
        {
            _window.set_speakers({});
            _window.set_play_enabled(false);
            _window.set_status(L"VOICEVOX 接続失敗: " + result->error);
            return true;
        }

        _window.set_speakers(result->speakers);
        _window.set_play_enabled(!result->speakers.empty());
        _window.set_status(
            L"VOICEVOX 接続済み (" +
            std::to_wstring(result->speakers.size()) +
            L" styles)"
        );
        _initialized = !result->speakers.empty();
        return true;
    }

    if (message == synthesis_ready_message)
    {
        if (_worker.joinable())
        {
            _worker.join();
        }

        std::unique_ptr<synthesis_result> result(reinterpret_cast<synthesis_result*>(l_param));
        _busy = false;

        if (!result->error.empty())
        {
            _window.set_play_enabled(_initialized);
            _window.set_status(L"音声合成失敗: " + result->error);
            return true;
        }

        if (!_player.play(result->wav_data))
        {
            _window.set_play_enabled(_initialized);
            _window.set_status(L"生成した WAV を再生できませんでした。");
            return true;
        }

        _window.set_play_enabled(_initialized);
        _window.set_status(L"音声を再生しています。");
        return true;
    }

    return false;
}

void tts_controller::start_worker(std::function<void()> task)
{
    if (_worker.joinable())
    {
        _worker.join();
    }

    _worker = std::thread(std::move(task));
}
