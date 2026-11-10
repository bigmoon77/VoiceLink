#pragma once

#include "audio_recorder.h"
#include "speech_recognition_engine.h"
#include "../text_to_speech/audio_player.h"

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
    struct training_sample
    {
        std::wstring wav_path;
        std::wstring text;
    };

    static constexpr wchar_t _window_class_name[] = L"VoiceLinkTrainingWindow";
    enum control_id
    {
        record_label_id = 2001,
        record_list_id,
        candidate_label_id,
        candidate_combo_id,
        correction_label_id,
        correction_edit_id,
        commit_button_id,
        adaptation_button_id,
        close_button_id,
        status_text_id,
        sample_label_id,
        sample_list_id,
        capture_button_id,
        sample_play_button_id,
        sample_save_button_id
    };

    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
    LRESULT handle_message(UINT message, WPARAM w_param, LPARAM l_param);
    bool create();
    bool create_controls();
    void layout_controls(int width, int height);
    void refresh_records();
    void load_training_history();
    void refresh_selected_record();
    void load_training_samples();
    void refresh_sample_list();
    void select_training_sample();
    void start_recording();
    void stop_recording();
    void play_selected_sample();
    void save_recorded_sample();
    std::wstring get_correction_text() const;
    bool save_training_text(const std::wstring& text) const;
    bool get_sample_directory(std::wstring& directory, bool create) const;
    bool read_file(const std::wstring& path, std::vector<std::uint8_t>& data) const;
    bool write_file(const std::wstring& path, const std::vector<std::uint8_t>& data) const;
    bool read_utf8_text(const std::wstring& path, std::wstring& text) const;
    bool write_utf8_text(const std::wstring& path, const std::wstring& text) const;
    std::wstring create_sample_base_name(const std::wstring& directory) const;
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
    HWND _sample_label = nullptr;
    HWND _sample_list = nullptr;
    HWND _capture_button = nullptr;
    HWND _sample_play_button = nullptr;
    HWND _sample_save_button = nullptr;

    std::vector<speech_recognition_result> _records;
    std::vector<training_sample> _samples;
    std::vector<std::uint8_t> _recorded_audio;
    std::vector<std::uint8_t> _selected_audio;
    audio_recorder _recorder;
    audio_player _audio_player;
    std::function<void(std::uint64_t, std::size_t)> _alternate_callback;
    std::function<void(const std::wstring&)> _adaptation_callback;
};
