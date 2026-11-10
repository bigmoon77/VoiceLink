#pragma once

#include <cstdint>
#include <vector>

class audio_player
{
public:
    audio_player() = default;
    ~audio_player();

    audio_player(const audio_player&) = delete;
    audio_player& operator=(const audio_player&) = delete;

    bool play(const std::vector<std::uint8_t>& wav_data);
    bool play_test_tone();
    void set_volume(int volume);
    int get_volume() const;
    void stop();

private:
    void apply_volume();

    std::vector<std::uint8_t> _wav_data;
    int _volume = 100;
};
