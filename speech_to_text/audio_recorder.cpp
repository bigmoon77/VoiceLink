#include "audio_recorder.h"

#include <algorithm>

#pragma comment(lib, "winmm.lib")

audio_recorder::~audio_recorder()
{
    std::wstring error;
    stop(error);
}

bool audio_recorder::start(HWND callback_window, std::wstring& error)
{
    if (!callback_window)
    {
        error = L"録音通知先のウィンドウが無効です。";
        return false;
    }

    if (_wave_in)
    {
        if (!stop(error))
        {
            return false;
        }
    }

    _callback_window = callback_window;
    _samples.clear();
    _samples.reserve(max_sample_count);

    _format = {};
    _format.wFormatTag = WAVE_FORMAT_PCM;
    _format.nChannels = 1;
    _format.nSamplesPerSec = sample_rate;
    _format.wBitsPerSample = 16;
    _format.nBlockAlign = static_cast<WORD>(
        _format.nChannels * _format.wBitsPerSample / 8
    );
    _format.nAvgBytesPerSec = _format.nSamplesPerSec * _format.nBlockAlign;

    MMRESULT result = waveInOpen(
        &_wave_in,
        WAVE_MAPPER,
        &_format,
        reinterpret_cast<DWORD_PTR>(_callback_window),
        0,
        CALLBACK_WINDOW
    );
    if (result != MMSYSERR_NOERROR)
    {
        _wave_in = nullptr;
        error = L"マイクを開けませんでした: " + get_wave_error(result);
        return false;
    }

    for (size_t index = 0; index < buffer_count; ++index)
    {
        _buffers[index].assign(buffer_sample_count, 0);
        WAVEHDR& header = _headers[index];
        header = {};
        header.lpData = reinterpret_cast<LPSTR>(_buffers[index].data());
        header.dwBufferLength = static_cast<DWORD>(
            _buffers[index].size() * sizeof(std::int16_t)
        );

        result = waveInPrepareHeader(_wave_in, &header, sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR)
        {
            error = L"録音バッファを準備できませんでした: " + get_wave_error(result);
            release_device();
            return false;
        }
        ++_prepared_count;

        result = waveInAddBuffer(_wave_in, &header, sizeof(WAVEHDR));
        if (result != MMSYSERR_NOERROR)
        {
            error = L"録音バッファを登録できませんでした: " + get_wave_error(result);
            release_device();
            return false;
        }
    }

    _recording = true;
    result = waveInStart(_wave_in);
    if (result != MMSYSERR_NOERROR)
    {
        _recording = false;
        error = L"録音を開始できませんでした: " + get_wave_error(result);
        release_device();
        return false;
    }

    error.clear();
    return true;
}

bool audio_recorder::stop(std::wstring& error)
{
    if (!_wave_in)
    {
        _recording = false;
        error.clear();
        return true;
    }

    _recording = false;

    MMRESULT stop_result = waveInStop(_wave_in);
    MMRESULT reset_result = waveInReset(_wave_in);

    if (_callback_window)
    {
        MSG message{};
        while (PeekMessageW(
            &message,
            _callback_window,
            MM_WIM_DATA,
            MM_WIM_DATA,
            PM_REMOVE
        ))
        {
            handle_message(message.message, message.wParam, message.lParam);
        }
    }

    MMRESULT release_result = MMSYSERR_NOERROR;
    for (size_t index = 0; index < _prepared_count; ++index)
    {
        const MMRESULT result = waveInUnprepareHeader(
            _wave_in,
            &_headers[index],
            sizeof(WAVEHDR)
        );
        if (result != MMSYSERR_NOERROR && release_result == MMSYSERR_NOERROR)
        {
            release_result = result;
        }
    }

    const MMRESULT close_result = waveInClose(_wave_in);
    if (close_result == MMSYSERR_NOERROR)
    {
        _wave_in = nullptr;
        _prepared_count = 0;
        _callback_window = nullptr;
        for (WAVEHDR& header : _headers)
        {
            header = {};
        }
    }

    if (reset_result != MMSYSERR_NOERROR)
    {
        error = L"録音デバイスを停止できませんでした: " + get_wave_error(reset_result);
        return false;
    }
    if (release_result != MMSYSERR_NOERROR)
    {
        error = L"録音バッファを解放できませんでした: " + get_wave_error(release_result);
        return false;
    }
    if (close_result != MMSYSERR_NOERROR)
    {
        error = L"録音デバイスを閉じられませんでした: " + get_wave_error(close_result);
        return false;
    }
    if (stop_result != MMSYSERR_NOERROR)
    {
        error = L"録音停止時にエラーが発生しました: " + get_wave_error(stop_result);
        return false;
    }

    error.clear();
    return true;
}

bool audio_recorder::handle_message(
    UINT message,
    WPARAM w_param,
    LPARAM l_param
)
{
    if (message != MM_WIM_DATA ||
        !_wave_in ||
        reinterpret_cast<HWAVEIN>(w_param) != _wave_in)
    {
        return false;
    }

    WAVEHDR* header = reinterpret_cast<WAVEHDR*>(l_param);
    if (!header)
    {
        return true;
    }

    const size_t sample_count =
        static_cast<size_t>(header->dwBytesRecorded / sizeof(std::int16_t));
    const size_t remaining =
        _samples.size() < max_sample_count
            ? max_sample_count - _samples.size()
            : 0;
    const size_t append_count = (std::min)(sample_count, remaining);
    const std::int16_t* pcm =
        reinterpret_cast<const std::int16_t*>(header->lpData);

    for (size_t index = 0; index < append_count; ++index)
    {
        _samples.push_back(static_cast<float>(pcm[index]) / 32768.0f);
    }

    if (_recording && _samples.size() < max_sample_count)
    {
        header->dwBytesRecorded = 0;
        const MMRESULT result = waveInAddBuffer(
            _wave_in,
            header,
            sizeof(WAVEHDR)
        );
        if (result == MMSYSERR_NOERROR)
        {
            return true;
        }
    }

    if (_recording)
    {
        _recording = false;
        waveInStop(_wave_in);
        waveInReset(_wave_in);
        if (_callback_window)
        {
            PostMessageW(_callback_window, limit_reached_message, 0, 0);
        }
    }

    return true;
}

bool audio_recorder::is_recording() const
{
    return _recording;
}

const std::vector<float>& audio_recorder::get_samples() const
{
    return _samples;
}

void audio_recorder::release_device()
{
    if (!_wave_in)
    {
        return;
    }

    _recording = false;
    waveInStop(_wave_in);
    waveInReset(_wave_in);

    if (_callback_window)
    {
        MSG message{};
        while (PeekMessageW(
            &message,
            _callback_window,
            MM_WIM_DATA,
            MM_WIM_DATA,
            PM_REMOVE
        ))
        {
            handle_message(message.message, message.wParam, message.lParam);
        }
    }

    for (size_t index = 0; index < _prepared_count; ++index)
    {
        waveInUnprepareHeader(_wave_in, &_headers[index], sizeof(WAVEHDR));
    }

    waveInClose(_wave_in);
    _wave_in = nullptr;
    _prepared_count = 0;
    _callback_window = nullptr;
    for (WAVEHDR& header : _headers)
    {
        header = {};
    }
}

std::wstring audio_recorder::get_wave_error(MMRESULT result)
{
    wchar_t message[MAXERRORLENGTH]{};
    if (waveInGetErrorTextW(result, message, MAXERRORLENGTH) == MMSYSERR_NOERROR)
    {
        return message;
    }

    return L"MMRESULT=" + std::to_wstring(result);
}
