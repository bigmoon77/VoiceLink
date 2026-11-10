#include "audio_recorder.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#pragma comment(lib, "winmm.lib")

audio_recorder::audio_recorder() = default;

audio_recorder::~audio_recorder()
{
    std::vector<std::uint8_t> ignored;
    std::wstring error;
    if (_wave_in) stop(ignored, error);
}

bool audio_recorder::start(std::wstring& error)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (_wave_in || _recording)
    {
        error = L"録音は既に開始されています。";
        return false;
    }

    _recorded_data.clear();

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = channel_count;
    format.nSamplesPerSec = sample_rate;
    format.wBitsPerSample = bits_per_sample;
    format.nBlockAlign = static_cast<WORD>(channel_count * bits_per_sample / 8);
    format.nAvgBytesPerSec = sample_rate * format.nBlockAlign;
    format.cbSize = 0;

    MMRESULT result = waveInOpen(
        &_wave_in,
        WAVE_MAPPER,
        &format,
        reinterpret_cast<DWORD_PTR>(&wave_in_callback),
        reinterpret_cast<DWORD_PTR>(this),
        CALLBACK_FUNCTION
    );
    if (result != MMSYSERR_NOERROR)
    {
        _wave_in = nullptr;
        error = L"マイクを開けませんでした。録音デバイスを確認してください。";
        return false;
    }

    for (std::size_t i = 0; i < buffer_count; ++i)
    {
        WAVEHDR& header = _headers[i];
        header = {};
        header.lpData = _buffers[i].data();
        header.dwBufferLength = static_cast<DWORD>(buffer_size);
        result = waveInPrepareHeader(_wave_in, &header, sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR)
        {
            error = L"録音バッファを初期化できませんでした。";
            release_buffers();
            waveInClose(_wave_in);
            _wave_in = nullptr;
            return false;
        }
        result = waveInAddBuffer(_wave_in, &header, sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR)
        {
            error = L"録音バッファを登録できませんでした。";
            release_buffers();
            waveInClose(_wave_in);
            _wave_in = nullptr;
            return false;
        }
    }

    _recording = true;
    result = waveInStart(_wave_in);
    if (result != MMSYSERR_NOERROR)
    {
        _recording = false;
        error = L"録音を開始できませんでした。";
        waveInStop(_wave_in);
        waveInReset(_wave_in);
        release_buffers();
        waveInClose(_wave_in);
        _wave_in = nullptr;
        return false;
    }
    return true;
}

bool audio_recorder::stop(std::vector<std::uint8_t>& wav_data, std::wstring& error)
{
    HWAVEIN wave_in = nullptr;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!_wave_in)
        {
            error = L"録音は開始されていません。";
            return false;
        }
        _recording = false;
        wave_in = _wave_in;
    }

    waveInStop(wave_in);
    waveInReset(wave_in);

    {
        std::lock_guard<std::mutex> lock(_mutex);
        release_buffers();
        waveInClose(_wave_in);
        _wave_in = nullptr;

        if (_recorded_data.empty())
        {
            error = L"録音データがありません。";
            return false;
        }

        wav_data.clear();
        wav_data.reserve(44 + _recorded_data.size());
        wav_data.insert(wav_data.end(), { 'R', 'I', 'F', 'F' });
        append_u32(wav_data, static_cast<DWORD>(36 + _recorded_data.size()));
        wav_data.insert(wav_data.end(), { 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ' });
        append_u32(wav_data, 16);
        append_u16(wav_data, 1);
        append_u16(wav_data, channel_count);
        append_u32(wav_data, sample_rate);
        append_u32(wav_data, sample_rate * channel_count * bits_per_sample / 8);
        append_u16(wav_data, channel_count * bits_per_sample / 8);
        append_u16(wav_data, bits_per_sample);
        wav_data.insert(wav_data.end(), { 'd', 'a', 't', 'a' });
        append_u32(wav_data, static_cast<DWORD>(_recorded_data.size()));
        wav_data.insert(wav_data.end(), _recorded_data.begin(), _recorded_data.end());
    }
    return true;
}

bool audio_recorder::is_recording() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _recording;
}

void CALLBACK audio_recorder::wave_in_callback(
    HWAVEIN,
    UINT message,
    DWORD_PTR instance,
    DWORD_PTR parameter_1,
    DWORD_PTR
)
{
    if (message != WIM_DATA || instance == 0 || parameter_1 == 0) return;
    audio_recorder* recorder = reinterpret_cast<audio_recorder*>(instance);
    recorder->on_buffer_done(reinterpret_cast<WAVEHDR*>(parameter_1));
}

void audio_recorder::on_buffer_done(WAVEHDR* header)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_wave_in || !header) return;

    if (header->dwBytesRecorded > 0)
    {
        const std::uint8_t* begin = reinterpret_cast<const std::uint8_t*>(header->lpData);
        _recorded_data.insert(_recorded_data.end(), begin, begin + header->dwBytesRecorded);
    }

    if (_recording)
    {
        header->dwBytesRecorded = 0;
        waveInAddBuffer(_wave_in, header, sizeof(WAVEHDR));
    }
}

void audio_recorder::release_buffers()
{
    if (!_wave_in) return;
    for (WAVEHDR& header : _headers)
    {
        if (header.dwFlags & WHDR_PREPARED)
        {
            waveInUnprepareHeader(_wave_in, &header, sizeof(WAVEHDR));
        }
        header = {};
    }
}

void audio_recorder::append_u16(std::vector<std::uint8_t>& data, WORD value)
{
    data.push_back(static_cast<std::uint8_t>(value & 0xFF));
    data.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void audio_recorder::append_u32(std::vector<std::uint8_t>& data, DWORD value)
{
    data.push_back(static_cast<std::uint8_t>(value & 0xFF));
    data.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    data.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    data.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
}
