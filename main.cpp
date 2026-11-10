#include "main_window.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command)
{
    main_window window;

    if (!window.create(instance, show_command))
    {
        MessageBoxW(
            nullptr,
            L"VoiceLink のウィンドウを作成できませんでした。",
            L"VoiceLink",
            MB_OK | MB_ICONERROR
        );
        return 1;
    }

    window.set_play_callback([&window]()
    {
        window.set_status(L"TTS backend はまだ接続されていません。");
    });

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
