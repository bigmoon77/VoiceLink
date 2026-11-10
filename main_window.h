#pragma once

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
    std::wstring get_text() const;
    bool get_selected_speaker_id(int& speaker_id) const;
    void set_play_callback(std::function<void()> callback);

private:
    static constexpr wchar_t _window_class_name[] = L"VoiceLinkMainWindow";

    enum control_id
    {
        speaker_label_id = 1001,
        speaker_combo_id,
        text_label_id,
        text_edit_id,
        play_button_id,
        status_text_id
    };

    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

    LRESULT handle_message(UINT message, WPARAM w_param, LPARAM l_param);
    bool create_controls();
    void layout_controls(int width, int height);
    void apply_control_font(HWND control);

    HINSTANCE _instance = nullptr;
    HWND _window = nullptr;
    HWND _speaker_label = nullptr;
    HWND _speaker_combo = nullptr;
    HWND _text_label = nullptr;
    HWND _text_edit = nullptr;
    HWND _play_button = nullptr;
    HWND _status_text = nullptr;
    std::function<void()> _play_callback;
};
