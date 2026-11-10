#pragma once

#include "speech_recognition_engine.h"

#include <Windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class training_window
{
public:
    training_window(HWND parent);
    ~training_window();

    training_window(const training_window&) = delete;
    training_window& operator=(const training_window&) = delete;

    void show();
    void add_record(const speech_recognition_result& record);
    void set_status(const std::wstring& status);
    void set_alternate_callback(std::function<void(std::uint64_t, std::size_t)> callback);
    void set_adaptation_callback(std::function<void(const std::wstring&)> callback);

private:
    static constexpr wchar_t _window_class_name[] = L"VoiceLinkTrainingWindow";
    enum control_id { record_label_id = 2001, record_list_id, candidate_label_id, candidate_combo_id, correction_label_id, correction_edit_id, commit_button_id, adaptation_button_id, close_button_id, status_text_id };
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
    LRESULT handle_message(UINT message, WPARAM w_param, LPARAM l_param);
    bool create();
    bool create_controls();
    void layout_controls(int width, int height);
    void refresh_records();
    void load_training_history();
    void refresh_selected_record();
    std::wstring get_correction_text() const;
    bool save_training_text(const std::wstring& text) const;
    void apply_font(HWND control);

    HWND _parent = nullptr;
    HWND _window = nullptr;
    HWND _record_label = nullptr;
    HWND _record_list = nullptr;
    HWND _candidate_label = nullptr;
    HWND _candidate_combo = nullptr;
    HWND _correction_label = nullptr;
    HWND _correction_edit = nullptr;
    HWND _commit_button = nullptr;
    HWND _adaptation_button = nullptr;
    HWND _close_button = nullptr;
    HWND _status_text = nullptr;
    std::vector<speech_recognition_result> _records;
    std::function<void(std::uint64_t, std::size_t)> _alternate_callback;
    std::function<void(const std::wstring&)> _adaptation_callback;
};
