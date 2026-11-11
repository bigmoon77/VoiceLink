#pragma once

#include "audio_recorder.h"
#include "../storage_system.h"
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
    // 想定頻度: トレーニング画面インスタンス生成時に1回。
    training_window(HWND parent);
    // 想定頻度: トレーニング画面インスタンス破棄時に1回。
    ~training_window();

    training_window(const training_window&) = delete;
    training_window& operator=(const training_window&) = delete;

    // 想定頻度: トレーニング画面を表示する操作ごとに1回。
    void show();
    // 想定頻度: 音声認識結果の受信ごとに1回。
    void add_record(const speech_recognition_result& record);
    // 想定頻度: GUI操作・処理結果の通知ごとに複数回。
    void set_status(const std::wstring& status);
    // 想定頻度: 初期化時に1回。
    void set_alternate_callback(std::function<void(std::uint64_t, std::size_t)> callback);
    // 想定頻度: 初期化時に1回。
    void set_adaptation_callback(std::function<void(const std::wstring&)> callback);
    // 想定頻度: 初期化時に1回。
    void set_batch_adaptation_callback(std::function<bool(const std::vector<std::wstring>&, std::size_t)> callback);

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
        sample_save_button_id,
        sample_update_button_id,
        open_directory_button_id,
        batch_count_label_id,
        batch_count_edit_id,
        batch_adaptation_button_id
    };

    // 想定頻度: ウィンドウメッセージごと。表示中は毎秒数回～数百回。
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
    // 想定頻度: ウィンドウメッセージごと。表示中は毎秒数回～数百回。
    LRESULT handle_message(UINT message, WPARAM w_param, LPARAM l_param);
    // 想定頻度: トレーニング画面インスタンスごとに最大1回。
    bool create();
    // 想定頻度: ウィンドウ生成時に1回。
    bool create_controls();
    // 想定頻度: 初回表示・ウィンドウサイズ変更時。通常は数回～数十回。
    void layout_controls(int width, int height);
    // 想定頻度: 認識結果追加・一覧更新時。通常は認識結果ごとに1回。
    void refresh_records();
    // 想定頻度: トレーニング画面生成時に1回。
    void load_training_history();
    // 想定頻度: 認識結果一覧の選択変更ごとに1回。
    void refresh_selected_record();
    // 想定頻度: トレーニング画面生成時に1回。保存済みWAV数に比例。
    void load_training_samples();
    // 想定頻度: サンプル追加・一覧再構築時。通常は保存ごとに1回。
    void refresh_sample_list();
    // 想定頻度: サンプル一覧で項目を選択したときに1回。
    void select_training_sample();
    // 想定頻度: 録音開始操作ごとに1回。
    void start_recording();
    // 想定頻度: 録音停止操作ごとに1回。
    void stop_recording();
    // 想定頻度: 音声再生操作ごとに1回。
    void play_selected_sample();
    // 想定頻度: 録音サンプル保存操作ごとに1回。
    void save_recorded_sample();
    // 想定頻度: 選択サンプルのテキスト更新操作ごとに1回。
    void update_selected_sample_text();
    // 想定頻度: 保存先を開く操作ごとに1回。
    void open_sample_directory();
    // 想定頻度: 一括適応操作ごとに1回。
    void submit_all_samples_for_adaptation();
    // 想定頻度: テキスト取得を伴うGUI操作ごとに1回。
    std::wstring get_correction_text() const;
    // 想定頻度: 認識結果を学習履歴へ保存するときに1回。
    bool save_training_text(const std::wstring& text) const;
    // 想定頻度: コントロール生成時にコントロール1個につき1回。
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
    HWND _sample_update_button = nullptr;
    HWND _open_directory_button = nullptr;
    HWND _batch_count_label = nullptr;
    HWND _batch_count_edit = nullptr;
    HWND _batch_adaptation_button = nullptr;

    std::vector<speech_recognition_result> _records;
    std::vector<training_sample> _samples;
    std::vector<std::uint8_t> _recorded_audio;
    std::vector<std::uint8_t> _selected_audio;
    audio_recorder _recorder;
    audio_player _audio_player;
    std::function<void(std::uint64_t, std::size_t)> _alternate_callback;
    std::function<void(const std::wstring&)> _adaptation_callback;
    std::function<bool(const std::vector<std::wstring>&, std::size_t)> _batch_adaptation_callback;
};
