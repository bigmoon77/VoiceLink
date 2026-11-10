#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <functional>
#include <string>
#include <vector>

class main_window
{
public:
    struct speaker_item
    {
        std::wstring name;
        int speaker_id = -1;
    };

    main_window() = default;
    ~main_window() = default;

    main_window(const main_window&) = delete;
    main_window& operator=(const main_window&) = delete;

    bool create(HINSTANCE instance, int show_command);
    void set_speakers(const std::vector<speaker_item>& speakers);
    void set_status(const std::wstring& message);
    void set_play_enabled(bool enabled);
    void set_recognition_start_enabled(bool enabled);
    void set_recognition_stop_enabled(bool enabled);
    void append_recognized_text(const std::wstring& text);
    void set_reconnect_enabled(bool enabled);
    void set_voicevox_path_enabled(bool enabled);
    void set_volume(int volume);
    void set_voicevox_path(const std::wstring& path);
    std::wstring get_voicevox_path() const;
    std::wstring get_text() const;
    bool get_selected_speaker_id(int& speaker_id) const;
    void set_play_callback(std::function<void()> callback);
    void set_recognition_start_callback(std::function<void()> callback);
    void set_recognition_stop_callback(std::function<void()> callback);
    void set_reconnect_callback(std::function<void()> callback);
    void set_volume_callback(std::function<void(int)> callback);
    void set_volume_test_callback(std::function<void()> callback);
    void set_voicevox_path_callback(std::function<void(const std::wstring&)> callback);
    void set_message_callback(std::function<bool(UINT, WPARAM, LPARAM)> callback);
    void set_speech_message_callback(std::function<bool(UINT, WPARAM, LPARAM)> callback);
    HWND get_handle() const;

private:
    static constexpr wchar_t _window_class_name[] = L"VoiceLinkMainWindow";

    enum control_id
    {
        speaker_label_id = 1001,
        speaker_combo_id,
        reconnect_button_id,
        text_label_id,
        text_edit_id,
        play_button_id,
        status_text_id,
        volume_label_id,
        volume_slider_id,
        volume_test_button_id,
        engine_path_label_id,
        engine_path_edit_id,
        engine_path_browse_button_id,
        engine_path_apply_button_id,
        recognition_start_button_id,
        recognition_stop_button_id
    };

    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

    LRESULT handle_message(UINT message, WPARAM w_param, LPARAM l_param);
    bool create_controls();
    void layout_controls(int width, int height);
    void apply_control_font(HWND control);
    void update_volume_label(int volume);
    void browse_voicevox_path();

    HINSTANCE _instance = nullptr;
    HWND _window = nullptr;
    HWND _engine_path_label = nullptr;
    HWND _engine_path_edit = nullptr;
    HWND _engine_path_browse_button = nullptr;
    HWND _engine_path_apply_button = nullptr;
    HWND _speaker_label = nullptr;
    HWND _speaker_combo = nullptr;
    HWND _reconnect_button = nullptr;
    HWND _text_label = nullptr;
    HWND _text_edit = nullptr;
    HWND _play_button = nullptr;
    HWND _recognition_start_button = nullptr;
    HWND _recognition_stop_button = nullptr;
    HWND _status_text = nullptr;
    HWND _volume_label = nullptr;
    HWND _volume_slider = nullptr;
    HWND _volume_test_button = nullptr;
    std::function<void()> _play_callback;
    std::function<void()> _recognition_start_callback;
    std::function<void()> _recognition_stop_callback;
    std::function<void()> _reconnect_callback;
    std::function<void(int)> _volume_callback;
    std::function<void()> _volume_test_callback;
    std::function<void(const std::wstring&)> _voicevox_path_callback;
    std::function<bool(UINT, WPARAM, LPARAM)> _message_callback;
    std::function<bool(UINT, WPARAM, LPARAM)> _speech_message_callback;
};
