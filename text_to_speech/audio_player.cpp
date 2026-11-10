#include "audio_player.h"

#include <Windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#pragma comment(lib, "winmm.lib")

namespace
{
    std::uint16_t read_u16(const std::uint8_t* data)
    {
        return static_cast<std::uint16_t>(data[0]) |
            (static_cast<std::uint16_t>(data[1]) << 8);
    }

    std::uint32_t read_u32(const std::uint8_t* data)
    {
        return static_cast<std::uint32_t>(data[0]) |
            (static_cast<std::uint32_t>(data[1]) << 8) |
            (static_cast<std::uint32_t>(data[2]) << 16) |
            (static_cast<std::uint32_t>(data[3]) << 24);
    }

    void write_u16(std::uint8_t* data, std::uint16_t value)
    {
        data[0] = static_cast<std::uint8_t>(value & 0xff);
        data[1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
    }

    void write_u32(std::uint8_t* data, std::uint32_t value)
    {
        data[0] = static_cast<std::uint8_t>(value & 0xff);
        data[1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
        data[2] = static_cast<std::uint8_t>((value >> 16) & 0xff);
        data[3] = static_cast<std::uint8_t>((value >> 24) & 0xff);
    }
}

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
    apply_volume();

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

bool audio_player::play_test_tone()
{
    constexpr std::uint32_t sample_rate = 44100;
    constexpr std::uint16_t channels = 1;
    constexpr std::uint16_t bits_per_sample = 16;
    constexpr std::uint32_t duration_ms = 600;
    constexpr std::uint32_t sample_count = sample_rate * duration_ms / 1000;
    constexpr std::uint32_t data_size = sample_count * channels * (bits_per_sample / 8);

    std::vector<std::uint8_t> wav_data(44 + data_size, 0);

    wav_data[0] = 'R';
    wav_data[1] = 'I';
    wav_data[2] = 'F';
    wav_data[3] = 'F';
    write_u32(wav_data.data() + 4, 36 + data_size);
    wav_data[8] = 'W';
    wav_data[9] = 'A';
    wav_data[10] = 'V';
    wav_data[11] = 'E';

    wav_data[12] = 'f';
    wav_data[13] = 'm';
    wav_data[14] = 't';
    wav_data[15] = ' ';
    write_u32(wav_data.data() + 16, 16);
    write_u16(wav_data.data() + 20, 1);
    write_u16(wav_data.data() + 22, channels);
    write_u32(wav_data.data() + 24, sample_rate);
    write_u32(wav_data.data() + 28, sample_rate * channels * bits_per_sample / 8);
    write_u16(wav_data.data() + 32, channels * bits_per_sample / 8);
    write_u16(wav_data.data() + 34, bits_per_sample);

    wav_data[36] = 'd';
    wav_data[37] = 'a';
    wav_data[38] = 't';
    wav_data[39] = 'a';
    write_u32(wav_data.data() + 40, data_size);

    constexpr double frequency = 440.0;
    constexpr double amplitude = 12000.0;
    for (std::uint32_t i = 0; i < sample_count; ++i)
    {
        const double phase = 2.0 * 3.14159265358979323846 * frequency * i / sample_rate;
        const auto sample = static_cast<std::int16_t>(std::sin(phase) * amplitude);
        write_u16(wav_data.data() + 44 + i * 2, static_cast<std::uint16_t>(sample));
    }

    return play(wav_data);
}

void audio_player::set_volume(int volume)
{
    _volume = std::clamp(volume, 0, 100);
}

int audio_player::get_volume() const
{
    return _volume;
}

void audio_player::stop()
{
    PlaySoundW(nullptr, nullptr, 0);
    _wav_data.clear();
}

void audio_player::apply_volume()
{
    if (_volume == 100 || _wav_data.size() < 12)
    {
        return;
    }

    if (_wav_data[0] != 'R' || _wav_data[1] != 'I' ||
        _wav_data[2] != 'F' || _wav_data[3] != 'F' ||
        _wav_data[8] != 'W' || _wav_data[9] != 'A' ||
        _wav_data[10] != 'V' || _wav_data[11] != 'E')
    {
        return;
    }

    std::uint16_t audio_format = 0;
    std::uint16_t bits_per_sample = 0;
    std::size_t data_offset = 0;
    std::size_t data_size = 0;

    std::size_t offset = 12;
    while (offset + 8 <= _wav_data.size())
    {
        const std::uint32_t chunk_size = read_u32(_wav_data.data() + offset + 4);
        const std::size_t chunk_data = offset + 8;
        if (chunk_size > _wav_data.size() - chunk_data)
        {
            return;
        }

        const std::uint8_t* chunk_id = _wav_data.data() + offset;
        if (chunk_id[0] == 'f' && chunk_id[1] == 'm' &&
            chunk_id[2] == 't' && chunk_id[3] == ' ')
        {
            if (chunk_size < 16)
            {
                return;
            }

            audio_format = read_u16(_wav_data.data() + chunk_data);
            bits_per_sample = read_u16(_wav_data.data() + chunk_data + 14);
        }
        else if (chunk_id[0] == 'd' && chunk_id[1] == 'a' &&
            chunk_id[2] == 't' && chunk_id[3] == 'a')
        {
            data_offset = chunk_data;
            data_size = chunk_size;
        }

        const std::size_t padded_size = static_cast<std::size_t>(chunk_size) + (chunk_size & 1u);
        if (padded_size > _wav_data.size() - chunk_data)
        {
            break;
        }
        offset = chunk_data + padded_size;
    }

    // VOICEVOX の標準出力である 16-bit PCM にゲインを適用する。
    if (audio_format != 1 || bits_per_sample != 16 ||
        data_offset == 0 || data_size < 2)
    {
        return;
    }

    for (std::size_t i = 0; i + 1 < data_size; i += 2)
    {
        const std::int16_t sample = static_cast<std::int16_t>(read_u16(_wav_data.data() + data_offset + i));
        const int scaled = static_cast<int>(sample) * _volume / 100;
        const auto output = static_cast<std::int16_t>(std::clamp(scaled, -32768, 32767));
        write_u16(_wav_data.data() + data_offset + i, static_cast<std::uint16_t>(output));
    }
}
