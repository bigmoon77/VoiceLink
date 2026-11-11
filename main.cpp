#include "app_settings.h"
#include "main_window.h"
#include "text_to_speech/tts_controller.h"
#include "speech_to_text/stt_controller.h"
#include "speech_to_text/sapi_engine.h"

#include <winrt/base.h>

#include <memory>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command)
{
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    app_settings settings;
    settings.load();

    main_window window;

    if (!window.create(instance, show_command))
    {
        MessageBoxW(
            nullptr,
            L"VoiceLink のウィンドウを作成できませんでした。",
            L"VoiceLink",
            MB_OK | MB_ICONERROR
        );
        winrt::uninit_apartment();
        return 1;
    }

    audio_player player;
    tts_controller controller(window, player);
    stt_controller speech_controller(window, std::make_unique<sapi_engine>());
    window.set_voicevox_path(settings.get_voicevox_path());
    controller.set_voicevox_path(settings.get_voicevox_path());
    controller.set_engine_type(
        settings.get_tts_engine_type() == app_settings::tts_engine_type::gpt_sovits
    );

    const app_settings::gpt_sovits_settings& saved_gpt_settings =
        settings.get_gpt_sovits_settings();
    gpt_sovits_engine::settings gpt_settings;
    gpt_settings.host = saved_gpt_settings.host;
    gpt_settings.port = saved_gpt_settings.port;
    gpt_settings.reference_audio_path = saved_gpt_settings.reference_audio_path;
    gpt_settings.prompt_text = saved_gpt_settings.prompt_text;
    gpt_settings.prompt_language = saved_gpt_settings.prompt_language;
    gpt_settings.text_language = saved_gpt_settings.text_language;
    controller.set_gpt_sovits_settings(gpt_settings);

    main_window::gpt_sovits_form_settings form_settings;
    form_settings.host = saved_gpt_settings.host;
    form_settings.port = saved_gpt_settings.port;
    form_settings.reference_audio_path = saved_gpt_settings.reference_audio_path;
    form_settings.prompt_text = saved_gpt_settings.prompt_text;
    form_settings.prompt_language = saved_gpt_settings.prompt_language;
    form_settings.text_language = saved_gpt_settings.text_language;
    window.set_gpt_sovits_settings(form_settings);

    window.set_selected_engine(
        settings.get_tts_engine_type() == app_settings::tts_engine_type::gpt_sovits ? 1 : 0
    );

    window.set_engine_changed_callback([&](int engine_index)
    {
        settings.set_tts_engine_type(
            engine_index == 1
                ? app_settings::tts_engine_type::gpt_sovits
                : app_settings::tts_engine_type::voicevox
        );
        if (!settings.save())
        {
            window.set_status(L"設定ファイルを保存できませんでした。");
            return;
        }

        controller.set_engine_type(engine_index == 1);
        controller.initialize_async();
    });

    window.set_gpt_sovits_settings_callback(
        [&](const main_window::gpt_sovits_form_settings& value)
        {
            app_settings::gpt_sovits_settings saved;
            saved.host = value.host;
            saved.port = value.port;
            saved.reference_audio_path = value.reference_audio_path;
            saved.prompt_text = value.prompt_text;
            saved.prompt_language = value.prompt_language;
            saved.text_language = value.text_language;

            settings.set_gpt_sovits_settings(saved);
            if (!settings.save())
            {
                window.set_status(L"GPT-SoVITS 設定を保存できませんでした。");
                return;
            }

            gpt_sovits_engine::settings engine_settings;
            engine_settings.host = saved.host;
            engine_settings.port = saved.port;
            engine_settings.reference_audio_path = saved.reference_audio_path;
            engine_settings.prompt_text = saved.prompt_text;
            engine_settings.prompt_language = saved.prompt_language;
            engine_settings.text_language = saved.text_language;
            controller.set_gpt_sovits_settings(engine_settings);
            controller.initialize_async();
            window.set_status(L"GPT-SoVITS 設定を保存しました。接続を確認しています...");
        }
    );

    window.set_voicevox_path_callback([&](const std::wstring& path)
    {
        settings.set_voicevox_path(path);
        if (!settings.save())
        {
            window.set_status(L"設定ファイルを保存できませんでした。");
            return;
        }

        controller.set_voicevox_path(path);
        controller.initialize_async();
    });

    controller.initialize_async();

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    winrt::uninit_apartment();
    return static_cast<int>(message.wParam);
}
