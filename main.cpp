#include "app_settings.h"
#include "main_window.h"
#include "tts_controller.h"

#include <winrt/base.h>

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
    window.set_voicevox_path(settings.get_voicevox_path());
    controller.set_voicevox_path(settings.get_voicevox_path());

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
