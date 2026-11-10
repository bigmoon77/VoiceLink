#include "audio_player.h"

#include <Windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

audio_player::~audio_player()
{
    stop();
}

bool audio_player::play(const std::vector<std::uint8_t>& wav_data)
{
    if (wav_data.size() < 12 ||
        wav_data[0] != 'R' || wav_data[1] != 'I' ||
        wav_data[2] != 'F' || wav_data[3] != 'F' ||
        wav_data[8] != 'W' || wav_data[9] != 'A' ||
        wav_data[10] != 'V' || wav_data[11] != 'E')
    {
        return false;
    }

    stop();
    _wav_data = wav_data;

    if (!PlaySoundW(
        reinterpret_cast<LPCWSTR>(_wav_data.data()),
        nullptr,
        SND_MEMORY | SND_ASYNC | SND_NODEFAULT
    ))
    {
        _wav_data.clear();
        return false;
    }

    return true;
}

void audio_player::stop()
{
    PlaySoundW(nullptr, nullptr, 0);
    _wav_data.clear();
}
