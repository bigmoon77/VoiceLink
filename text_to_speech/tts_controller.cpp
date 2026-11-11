#include "tts_controller.h"
#include "voicevox_engine.h"
#include "gpt_sovits_engine.h"

#include <winrt/base.h>

#include <utility>

tts_controller::tts_controller(main_window& window, audio_player& player)
    : _window(window)
    , _player(player)
    , _engine(std::make_unique<voicevox_engine>())
{
    _window.set_play_callback([this]()
    {
        on_play();
    });

    _window.set_reconnect_callback([this]()
    {
        initialize_async();
    });

    _window.set_volume_callback([this](int volume)
    {
        _player.set_volume(volume);
    });

    _window.set_volume_test_callback([this]()
    {
        on_volume_test();
    });

    _window.set_message_callback([this](UINT message, WPARAM w_param, LPARAM l_param)
    {
        return handle_window_message(message, w_param, l_param);
    });

    _window.set_play_enabled(false);
    _window.set_reconnect_enabled(true);
    _window.set_voicevox_path_enabled(true);
}

tts_controller::~tts_controller()
{
    _window.set_play_callback({});
    _window.set_reconnect_callback({});
    _window.set_volume_callback({});
    _window.set_volume_test_callback({});
    _window.set_message_callback({});

    if (_worker.joinable())
    {
        _worker.join();
    }

    _player.stop();
}

void tts_controller::set_voicevox_path(const std::wstring& path)
{
    _voicevox_path = path;
}

void tts_controller::set_engine_type(bool use_gpt_sovits)
{
    if (_busy)
    {
        return;
    }

    _use_gpt_sovits = use_gpt_sovits;
    if (_use_gpt_sovits)
    {
        std::unique_ptr<gpt_sovits_engine> engine =
            std::make_unique<gpt_sovits_engine>();
        engine->set_settings(_gpt_sovits_settings);
        _engine = std::move(engine);
    }
    else
    {
        _engine = std::make_unique<voicevox_engine>();
    }
}

void tts_controller::set_gpt_sovits_settings(const gpt_sovits_engine::settings& value)
{
    if (_busy)
    {
        return;
    }

    _gpt_sovits_settings = value;
    if (_use_gpt_sovits)
    {
        gpt_sovits_engine* engine = dynamic_cast<gpt_sovits_engine*>(_engine.get());
        if (engine)
        {
            engine->set_settings(_gpt_sovits_settings);
        }
    }
}

void tts_controller::initialize_async()
{
    if (_busy.exchange(true))
    {
        return;
    }

    _initialized = false;
    _window.set_play_enabled(false);
    _window.set_reconnect_enabled(false);
    _window.set_voicevox_path_enabled(false);
    _window.set_status(_use_gpt_sovits
        ? L"GPT-SoVITS API に接続しています..."
        : L"VOICEVOX Engine に接続しています...");

    const std::wstring engine_path = _voicevox_path;
    const bool use_gpt_sovits = _use_gpt_sovits;

    start_worker([this, engine_path, use_gpt_sovits]()
    {
        speakers_result result;
        std::vector<tts_engine::speaker_style> engine_speakers;
        bool apartment_initialized = false;

        try
        {
            if (!use_gpt_sovits && !_voicevox_process.start(engine_path, result.error))
            {
                // Keep the connection failure in the normal initialization result.
            }
            else
            {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
                apartment_initialized = true;

                if (_engine->initialize(engine_speakers, result.error))
                {
                    result.speakers.reserve(engine_speakers.size());

                    for (const auto& speaker : engine_speakers)
                    {
                        std::wstring display_name = speaker.speaker_name;
                        display_name += L" / ";
                        display_name += speaker.style_name;

                        result.speakers.push_back({ std::move(display_name), speaker.speaker_id });
                    }
                }
            }
        }
        catch (const winrt::hresult_error& exception)
        {
            result.error = use_gpt_sovits
                ? L"GPT-SoVITS 初期化に失敗しました: "
                : L"VOICEVOX 初期化に失敗しました: ";
            result.error += exception.message().c_str();
        }
        catch (...)
        {
            result.error = use_gpt_sovits
                ? L"GPT-SoVITS 初期化中に予期しないエラーが発生しました。"
                : L"VOICEVOX 初期化中に予期しないエラーが発生しました。";
        }

        if (apartment_initialized)
        {
            winrt::uninit_apartment();
        }

        {
            std::lock_guard<std::mutex> lock(_result_mutex);
            _speakers_result = std::move(result);
        }

        if (!PostMessageW(_window.get_handle(), speakers_ready_message, 0, 0))
        {
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
        synthesis_result result;

        try
        {
            _engine->synthesize(text, speaker_id, result.wav_data, result.error);
        }
        catch (...)
        {
            result.error = L"音声合成中に予期しないエラーが発生しました。";
        }

        {
            std::lock_guard<std::mutex> lock(_result_mutex);
            _synthesis_result = std::move(result);
        }

        if (!PostMessageW(_window.get_handle(), synthesis_ready_message, 0, 0))
        {
            _busy = false;
        }
    });
}

void tts_controller::on_volume_test()
{
    if (!_player.play_test_tone())
    {
        _window.set_status(L"音量確認用のテスト音を再生できませんでした。");
        return;
    }

    _window.set_status(L"音量確認用のテスト音を再生しています。");
}

bool tts_controller::handle_window_message(UINT message, WPARAM, LPARAM)
{
    if (message == speakers_ready_message)
    {
        if (_worker.joinable())
        {
            _worker.join();
        }

        speakers_result result;
        {
            std::lock_guard<std::mutex> lock(_result_mutex);
            result = std::move(_speakers_result);
        }
        _busy = false;
        _window.set_reconnect_enabled(true);
        _window.set_voicevox_path_enabled(true);

        if (!result.error.empty())
        {
            _window.set_speakers({});
            _window.set_play_enabled(false);
            _window.set_status((_use_gpt_sovits ? L"GPT-SoVITS 接続失敗: " : L"VOICEVOX 接続失敗: ") + result.error);
            _initialized = false;
            return true;
        }

        _window.set_speakers(result.speakers);
        _window.set_play_enabled(!result.speakers.empty());
        _window.set_status(
            (_use_gpt_sovits ? L"GPT-SoVITS 接続済み (" : L"VOICEVOX 接続済み (") +
            std::to_wstring(result.speakers.size()) +
            L" styles)"
        );
        _initialized = !result.speakers.empty();
        return true;
    }

    if (message == synthesis_ready_message)
    {
        if (_worker.joinable())
        {
            _worker.join();
        }

        synthesis_result result;
        {
            std::lock_guard<std::mutex> lock(_result_mutex);
            result = std::move(_synthesis_result);
        }
        _busy = false;

        if (!result.error.empty())
        {
            _window.set_play_enabled(_initialized);
            _window.set_status(L"音声合成失敗: " + result.error);
            return true;
        }

        if (!_player.play(result.wav_data))
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
