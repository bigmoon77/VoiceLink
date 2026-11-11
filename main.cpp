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
