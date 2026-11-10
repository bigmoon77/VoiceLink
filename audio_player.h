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
    void stop();

private:
    std::vector<std::uint8_t> _wav_data;
};
