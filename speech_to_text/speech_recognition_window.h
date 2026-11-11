#pragma once

#include "audio_recorder.h"
#include "reazonspeech_engine.h"

#include <Windows.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class speech_recognition_window
{
public:
    speech_recognition_window() = default;
    ~speech_recognition_window();

    speech_recognition_window(const speech_recognition_window&) = delete;
    speech_recognition_window& operator=(const speech_recognition_window&) = delete;

    bool show(
        HINSTANCE instance,
        HWND owner,
        std::function<void(const std::wstring&)> apply_callback
    );

    void close();

private:
    static constexpr wchar_t window_class_name[] = L"VoiceLinkSpeechRecognitionWindow";
    static constexpr int record_button_id = 2001;
    static constexpr int apply_button_id = 2002;
    static constexpr int result_edit_id = 2003;
    static constexpr int status_text_id = 2004;
    static constexpr UINT initialize_completed_message = WM_APP + 22;
    static constexpr UINT recognition_completed_message = WM_APP + 23;

    static LRESULT CALLBACK window_proc(
        HWND window,
        UINT message,
        WPARAM w_param,
        LPARAM l_param
    );

    LRESULT handle_message(UINT message, WPARAM w_param, LPARAM l_param);
    bool create_controls();
    void layout_controls(int width, int height);
    void apply_control_font(HWND control);
    void set_status(const std::wstring& message);
    void start_initialize();
    void start_recording();
    void finish_recording();
    void start_recognition(const std::vector<float>& samples);
    void apply_result();

    HINSTANCE _instance = nullptr;
    HWND _owner = nullptr;
    HWND _window = nullptr;
    HWND _record_button = nullptr;
    HWND _apply_button = nullptr;
    HWND _result_edit = nullptr;
    HWND _status_text = nullptr;

    audio_recorder _recorder;
    reazonspeech_engine _engine;
    std::function<void(const std::wstring&)> _apply_callback;

    std::thread _worker;
    std::mutex _worker_mutex;
    std::wstring _worker_error;
    std::wstring _worker_text;
    bool _worker_success = false;
    bool _engine_ready = false;
    std::atomic<bool> _closing{ false };
};
