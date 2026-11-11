#include "training_window.h"
#include "../storage_system.h"

#include <algorithm>
#include <cwchar>
#include <limits>
#include <string>
#include <utility>
#include <shellapi.h>
#include <vector>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")

constexpr wchar_t training_window::_window_class_name[];

namespace\n{\n    constexpr int window_width = 760;\n    constexpr int window_height = 820;\n    constexpr int margin = 12;\n    constexpr int control_height = 26;\n    constexpr int label_height = 20;\n}

training_window::training_window(HWND parent)
    : _parent(parent)
{
}

training_window::~training_window()
{
    _audio_player.stop();
    if (_recorder.is_recording())
    {
        std::vector<std::uint8_t> ignored;
        std::wstring error;
        _recorder.stop(ignored, error);
    }
    if (_window)
    {
        DestroyWindow(_window);
        _window = nullptr;
    }
}

void training_window::show()
{
    if (!_window && !create())
    {
        return;
    }
    ShowWindow(_window, SW_SHOW);
    SetForegroundWindow(_window);
}

void training_window::add_record(const speech_recognition_result& record)
{
    _records.push_back(record);
    if (_records.size() > 100)
    {
        _records.erase(_records.begin());
    }
    if (_record_list)
    {
        refresh_records();
        const LRESULT count = SendMessageW(_record_list, LB_GETCOUNT, 0, 0);
        if (count > 0)
        {
            SendMessageW(_record_list, LB_SETCURSEL, static_cast<WPARAM>(count - 1), 0);
            refresh_selected_record();
        }
    }
}

void training_window::set_status(const std::wstring& status)
{
    if (_status_text) SetWindowTextW(_status_text, status.c_str());
}

void training_window::set_alternate_callback(std::function<void(std::uint64_t, std::size_t)> callback)
{
    _alternate_callback = std::move(callback);
}

void training_window::set_adaptation_callback(std::function<void(const std::wstring&)> callback)
{
    _adaptation_callback = std::move(callback);
}

void training_window::set_batch_adaptation_callback(std::function<bool(const std::vector<std::wstring>&, std::size_t)> callback)
{
    _batch_adaptation_callback = std::move(callback);
}

bool training_window::create()
{
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    window_class.lpszClassName = _window_class_name;
    if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    _window = CreateWindowExW(WS_EX_TOOLWINDOW, _window_class_name, L"SAPI Training", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, window_width, window_height, _parent, nullptr, window_class.hInstance, this);
    return _window != nullptr;
}

LRESULT CALLBACK training_window::window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    training_window* self = reinterpret_cast<training_window*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const auto* create_struct = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        self = static_cast<training_window*>(create_struct->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->_window = window;
    }
    return self ? self->handle_message(message, w_param, l_param) : DefWindowProcW(window, message, w_param, l_param);
}

LRESULT training_window::handle_message(UINT message, WPARAM w_param, LPARAM l_param)
{
    switch (message)
    {
    case WM_CREATE:
        return create_controls() ? 0 : -1;
    case WM_SIZE:
        layout_controls(LOWORD(l_param), HIWORD(l_param));
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w_param))
        {
        case record_list_id:
            if (HIWORD(w_param) == LBN_SELCHANGE) refresh_selected_record();
            return 0;
        case sample_list_id:
            if (HIWORD(w_param) == LBN_SELCHANGE) select_training_sample();
            return 0;
        case candidate_combo_id:
            if (HIWORD(w_param) == CBN_SELCHANGE)
            {
                const LRESULT record_index = SendMessageW(_record_list, LB_GETCURSEL, 0, 0);
                const LRESULT candidate_index = SendMessageW(_candidate_combo, CB_GETCURSEL, 0, 0);
                if (record_index != LB_ERR && candidate_index != CB_ERR &&
                    static_cast<std::size_t>(record_index) < _records.size())
                {
                    const speech_recognition_result& record = _records[static_cast<std::size_t>(record_index)];
                    const std::wstring& candidate = candidate_index == 0 ? record.text : record.alternatives[static_cast<std::size_t>(candidate_index - 1)];
                    SetWindowTextW(_correction_edit, candidate.c_str());
                }
            }
            return 0;
        case commit_button_id:
            if (HIWORD(w_param) == BN_CLICKED && _alternate_callback)
            {
                const LRESULT record_index = SendMessageW(_record_list, LB_GETCURSEL, 0, 0);
                const LRESULT candidate_index = SendMessageW(_candidate_combo, CB_GETCURSEL, 0, 0);
                if (record_index != LB_ERR && candidate_index > 0 &&
                    static_cast<std::size_t>(record_index) < _records.size())
                {
                    const speech_recognition_result& record = _records[static_cast<std::size_t>(record_index)];
                    _alternate_callback(record.id, static_cast<std::size_t>(candidate_index - 1));
                }
                else set_status(L"確定する代替候補を選択してください。");
            }
            return 0;
        case adaptation_button_id:
            if (HIWORD(w_param) == BN_CLICKED && _adaptation_callback)
            {
                const std::wstring text = get_correction_text();
                if (text.empty()) set_status(L"学習データが空です。");
                else if (!save_training_text(text)) set_status(L"訂正履歴を保存できませんでした。");
                else
                {
                    speech_recognition_result saved_record;
                    saved_record.text = text;
                    add_record(saved_record);
                    _adaptation_callback(text);
                }
            }
            return 0;
        case capture_button_id:
            if (HIWORD(w_param) == BN_CLICKED)
            {
                if (_recorder.is_recording()) stop_recording();
                else start_recording();
            }
            return 0;
        case sample_play_button_id:
            if (HIWORD(w_param) == BN_CLICKED) play_selected_sample();
            return 0;
        case sample_save_button_id:
            if (HIWORD(w_param) == BN_CLICKED) save_recorded_sample();
            return 0;
        case sample_update_button_id:
            if (HIWORD(w_param) == BN_CLICKED) update_selected_sample_text();
            return 0;
        case open_directory_button_id:
            if (HIWORD(w_param) == BN_CLICKED) open_sample_directory();
            return 0;
        case batch_adaptation_button_id:
            if (HIWORD(w_param) == BN_CLICKED) submit_all_samples_for_adaptation();
            return 0;
        case close_button_id:
            if (HIWORD(w_param) == BN_CLICKED) ShowWindow(_window, SW_HIDE);
            return 0;
        }
        break;
    case WM_CLOSE:
        ShowWindow(_window, SW_HIDE);
        return 0;
    }
    return DefWindowProcW(_window, message, w_param, l_param);
}

bool training_window::create_controls()
{
    HINSTANCE instance = GetModuleHandleW(nullptr);
    _record_label = CreateWindowExW(0, L"STATIC", L"認識履歴", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(record_label_id)), instance, nullptr);
    _record_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(record_list_id)), instance, nullptr);
    _candidate_label = CreateWindowExW(0, L"STATIC", L"認識候補", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(candidate_label_id)), instance, nullptr);
    _candidate_combo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(candidate_combo_id)), instance, nullptr);
    _correction_label = CreateWindowExW(0, L"STATIC", L"正解テキスト / 訂正後の文章", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(correction_label_id)), instance, nullptr);
    _correction_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(correction_edit_id)), instance, nullptr);
    _commit_button = CreateWindowExW(0, L"BUTTON", L"候補を確定", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(commit_button_id)), instance, nullptr);
    _adaptation_button = CreateWindowExW(0, L"BUTTON", L"訂正内容を学習", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(adaptation_button_id)), instance, nullptr);
    _close_button = CreateWindowExW(0, L"BUTTON", L"閉じる", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(close_button_id)), instance, nullptr);
    _status_text = CreateWindowExW(0, L"STATIC", L"認識結果を選択して訂正できます。録音した音声はサンプルとして保存できます。", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(status_text_id)), instance, nullptr);
    _sample_label = CreateWindowExW(0, L"STATIC", L"録音・音声サンプル", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(sample_label_id)), instance, nullptr);
    _sample_list = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(sample_list_id)), instance, nullptr);
    _capture_button = CreateWindowExW(0, L"BUTTON", L"録音開始", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(capture_button_id)), instance, nullptr);
    _sample_play_button = CreateWindowExW(0, L"BUTTON", L"選択音声を再生", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(sample_play_button_id)), instance, nullptr);
    _sample_save_button = CreateWindowExW(0, L"BUTTON", L"録音を保存", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(sample_save_button_id)), instance, nullptr);
    _sample_update_button = CreateWindowExW(0, L"BUTTON", L"テキスト更新", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(sample_update_button_id)), instance, nullptr);
    _open_directory_button = CreateWindowExW(0, L"BUTTON", L"保存先を開く", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(open_directory_button_id)), instance, nullptr);
    _batch_count_label = CreateWindowExW(0, L"STATIC", L"全サンプルの適応回数", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(batch_count_label_id)), instance, nullptr);
    _batch_count_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(batch_count_edit_id)), instance, nullptr);
    _batch_adaptation_button = CreateWindowExW(0, L"BUTTON", L"全サンプルを一括適応", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(batch_adaptation_button_id)), instance, nullptr);

    if (!_record_label || !_record_list || !_candidate_label || !_candidate_combo || !_correction_label ||
        !_correction_edit || !_commit_button || !_adaptation_button || !_close_button || !_status_text ||
        !_sample_label || !_sample_list || !_capture_button || !_sample_play_button || !_sample_save_button || !_sample_update_button || !_open_directory_button || !_batch_count_label || !_batch_count_edit || !_batch_adaptation_button)
    {
        return false;
    }

    apply_font(_record_label);
    apply_font(_record_list);
    apply_font(_candidate_label);
    apply_font(_candidate_combo);
    apply_font(_correction_label);
    apply_font(_correction_edit);
    apply_font(_commit_button);
    apply_font(_adaptation_button);
    apply_font(_close_button);
    apply_font(_status_text);
    apply_font(_sample_label);
    apply_font(_sample_list);
    apply_font(_capture_button);
    apply_font(_sample_play_button);
    apply_font(_sample_save_button);
    apply_font(_sample_update_button);
    apply_font(_open_directory_button);
    apply_font(_batch_count_label);
    apply_font(_batch_count_edit);
    apply_font(_batch_adaptation_button);

    load_training_history();
    refresh_records();
    load_training_samples();
    refresh_sample_list();
    return true;
}

void training_window::layout_controls(int width, int height)
{
    if (!_record_label || !_record_list || !_candidate_label || !_candidate_combo || !_correction_label ||
        !_correction_edit || !_commit_button || !_adaptation_button || !_close_button || !_status_text ||
        !_sample_label || !_sample_list || !_capture_button || !_sample_play_button || !_sample_save_button || !_sample_update_button || !_open_directory_button || !_batch_count_label || !_batch_count_edit || !_batch_adaptation_button)
    {
        return;
    }

    const int content_width = (std::max)(width - margin * 2, 120);
    const int list_label_y = margin;
    const int list_y = list_label_y + label_height;
    const int list_height = (std::max)(height / 4, 100);
    const int candidate_y = list_y + list_height + margin;
    const int correction_y = candidate_y + label_height + control_height + margin;
    const int buttons_y = correction_y + label_height + control_height + margin;
    const int status_y = buttons_y + control_height + margin;
    const int sample_label_y = status_y + label_height + margin;
    const int sample_list_y = sample_label_y + label_height;
    const int sample_list_height = (std::max)(height - sample_list_y - 4 * control_height - 6 * margin - label_height, 80);
    const int sample_buttons_y = sample_list_y + sample_list_height + margin;
    const int button_width = 125;

    MoveWindow(_record_label, margin, list_label_y, content_width, label_height, TRUE);
    MoveWindow(_record_list, margin, list_y, content_width, list_height, TRUE);
    MoveWindow(_candidate_label, margin, candidate_y, content_width, label_height, TRUE);
    MoveWindow(_candidate_combo, margin, candidate_y + label_height, content_width, control_height, TRUE);
    MoveWindow(_correction_label, margin, correction_y, content_width, label_height, TRUE);
    MoveWindow(_correction_edit, margin, correction_y + label_height, content_width, control_height, TRUE);
    MoveWindow(_commit_button, margin, buttons_y, button_width, control_height, TRUE);
    MoveWindow(_adaptation_button, margin + button_width + margin, buttons_y, button_width + 30, control_height, TRUE);
    MoveWindow(_close_button, width - margin - button_width, buttons_y, button_width, control_height, TRUE);
    MoveWindow(_status_text, margin, status_y, content_width, label_height, TRUE);
    MoveWindow(_sample_label, margin, sample_label_y, content_width, label_height, TRUE);
    MoveWindow(_sample_list, margin, sample_list_y, content_width, sample_list_height, TRUE);
    MoveWindow(_capture_button, margin, sample_buttons_y, button_width, control_height, TRUE);
    MoveWindow(_sample_play_button, margin + button_width + margin, sample_buttons_y, button_width + 15, control_height, TRUE);
    MoveWindow(_sample_save_button, margin + (button_width + margin) * 2 + 15, sample_buttons_y, button_width, control_height, TRUE);
    MoveWindow(_sample_update_button, margin + (button_width + margin) * 3 + 15, sample_buttons_y, button_width, control_height, TRUE);
    const int batch_y = sample_buttons_y + control_height + margin;
    MoveWindow(_batch_count_label, margin, batch_y, 150, control_height, TRUE);
    MoveWindow(_batch_count_edit, margin + 155, batch_y, 70, control_height, TRUE);
    MoveWindow(_batch_adaptation_button, margin + 235, batch_y, 220, control_height, TRUE);
    MoveWindow(_open_directory_button, margin + 470, batch_y, 150, control_height, TRUE);
}

void training_window::refresh_records()
{
    if (!_record_list) return;
    SendMessageW(_record_list, LB_RESETCONTENT, 0, 0);
    for (const speech_recognition_result& record : _records)
    {
        std::wstring label = record.text;
        if (label.size() > 90) { label.resize(90); label += L"..."; }
        SendMessageW(_record_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
}

void training_window::load_training_history()
{
    wchar_t app_data[MAX_PATH]{};
    const DWORD path_length = GetEnvironmentVariableW(L"APPDATA", app_data, MAX_PATH);
    if (path_length == 0 || path_length >= MAX_PATH) return;

    const std::wstring file_path = std::wstring(app_data) + L"\\VoiceLink\\sapi_training.txt";
    std::ifstream file(file_path, std::ios::binary);
    if (!file) return;

    std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
        static_cast<unsigned char>(data[1]) == 0xBB && static_cast<unsigned char>(data[2]) == 0xBF)
    {
        data.erase(0, 3);
    }

    size_t offset = 0;
    while (offset < data.size())
    {
        const size_t line_end = data.find('\n', offset);
        const size_t count = (line_end == std::string::npos ? data.size() : line_end) - offset;
        std::string line = data.substr(offset, count);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty())
        {
            const int wide_count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, line.data(), static_cast<int>(line.size()), nullptr, 0);
            if (wide_count > 0)
            {
                std::wstring wide_text(static_cast<size_t>(wide_count), L'\0');
                if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, line.data(), static_cast<int>(line.size()), wide_text.data(), wide_count) == wide_count)
                {
                    speech_recognition_result record;
                    record.text = std::move(wide_text);
                    _records.push_back(std::move(record));
                    if (_records.size() > 100) _records.erase(_records.begin());
                }
            }
        }
        if (line_end == std::string::npos) break;
        offset = line_end + 1;
    }
}

void training_window::refresh_selected_record()
{
    const LRESULT selected = SendMessageW(_record_list, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR || static_cast<std::size_t>(selected) >= _records.size()) return;
    const speech_recognition_result& record = _records[static_cast<std::size_t>(selected)];
    SendMessageW(_candidate_combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(_candidate_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(record.text.c_str()));
    for (const std::wstring& alternative : record.alternatives)
    {
        SendMessageW(_candidate_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(alternative.c_str()));
    }
    SendMessageW(_candidate_combo, CB_SETCURSEL, 0, 0);
    SetWindowTextW(_correction_edit, record.text.c_str());
}

void training_window::load_training_samples()
{
    std::wstring directory;
    directory = storage_system::get_directory_path(storage_system::directory::training_samples, true);\n    if (directory.empty()) return;

    const std::wstring search_path = directory + L"\\*.wav";
    WIN32_FIND_DATAW find_data{};
    HANDLE find_handle = FindFirstFileW(search_path.c_str(), &find_data);
    if (find_handle == INVALID_HANDLE_VALUE) return;

    do
    {
        if ((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        const std::wstring sample_stem = directory + L"\\\\" + find_data.cFileName;\n        const std::size_t extension_pos = sample_stem.rfind(L".wav");\n        if (extension_pos == std::wstring::npos) continue;\n        const std::wstring lock_path = sample_stem.substr(0, extension_pos) + L".lock";\n        if (GetFileAttributesW(lock_path.c_str()) != INVALID_FILE_ATTRIBUTES) continue;\n        training_sample sample;
        sample.wav_path = directory + L"\\" + find_data.cFileName;
        const std::size_t extension = sample.wav_path.rfind(L".wav");
        if (extension == std::wstring::npos) continue;
        const std::wstring text_path = sample.wav_path.substr(0, extension) + L".txt";
        if (!storage_system::read_utf8_text(text_path, sample.text)) continue;
        _samples.push_back(std::move(sample));
    } while (FindNextFileW(find_handle, &find_data));
    FindClose(find_handle);
}

void training_window::refresh_sample_list()
{
    if (!_sample_list) return;
    SendMessageW(_sample_list, LB_RESETCONTENT, 0, 0);
    for (const training_sample& sample : _samples)
    {
        const std::size_t slash = sample.wav_path.find_last_of(L"\\/");
        const std::wstring label = slash == std::wstring::npos ? sample.wav_path : sample.wav_path.substr(slash + 1);
        SendMessageW(_sample_list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
}

void training_window::select_training_sample()
{
    const LRESULT selected = SendMessageW(_sample_list, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR || static_cast<std::size_t>(selected) >= _samples.size()) return;

    const training_sample& sample = _samples[static_cast<std::size_t>(selected)];
    _recorded_audio.clear();
    std::vector<std::uint8_t> audio_data;
    if (!storage_system::read_binary(sample.wav_path, audio_data) || audio_data.size() < 44)
    {
        set_status(L"選択した音声サンプルを読み込めませんでした。");
        return;
    }

    _selected_audio = std::move(audio_data);
    SetWindowTextW(_correction_edit, sample.text.c_str());
    set_status(L"サンプルを読み込みました。音声を再生して正解テキストを確認できます。");
}

void training_window::start_recording()
{
    std::wstring error;
    _audio_player.stop();
    _recorded_audio.clear();
    _selected_audio.clear();
    if (!_recorder.start(error))
    {
        set_status(error);
        return;
    }
    SetWindowTextW(_capture_button, L"録音停止");
    set_status(L"録音中です。発話が終わったら録音停止を押してください。");
}

void training_window::stop_recording()
{
    std::vector<std::uint8_t> wav_data;
    std::wstring error;
    if (!_recorder.stop(wav_data, error))
    {
        SetWindowTextW(_capture_button, L"録音開始");
        set_status(error);
        return;
    }
    _recorded_audio = wav_data;
    _selected_audio = std::move(wav_data);
    SetWindowTextW(_capture_button, L"録音開始");
    set_status(L"録音を取得しました。正解テキストを入力して「録音を保存」を押してください。");
}

void training_window::play_selected_sample()
{
    if (_selected_audio.empty())
    {
        set_status(L"再生する音声がありません。録音するか、サンプル一覧から選択してください。");
        return;
    }
    if (!_audio_player.play(_selected_audio))
    {
        set_status(L"音声を再生できませんでした。WAV データを確認してください。");
        return;
    }
    set_status(L"音声を再生しています。");
}

void training_window::save_recorded_sample()
{
    if (_recorder.is_recording())
    {
        set_status(L"先に録音を停止してください。");
        return;
    }
    if (_recorded_audio.empty())
    {
        set_status(L"保存する録音データがありません。先に録音してください。");
        return;
    }

    const std::wstring text = get_correction_text();
    if (text.empty())
    {
        set_status(L"正解テキストを入力してください。");
        return;
    }

    std::wstring wav_path;
    if (!storage_system::save_training_sample(_recorded_audio, text, wav_path))
    {
        set_status(L"WAV/TXT ペアを保存できませんでした。");
        return;
    }

    training_sample sample;
    sample.wav_path = wav_path;
    sample.text = text;
    _samples.push_back(std::move(sample));
    refresh_sample_list();
    SendMessageW(_sample_list, LB_SETCURSEL, static_cast<WPARAM>(_samples.size() - 1), 0);
    _selected_audio = _recorded_audio;
    _recorded_audio.clear();
    set_status(L"音声と正解テキストを保存しました。");
}

void training_window::submit_all_samples_for_adaptation()
{
    if (!_batch_adaptation_callback)
    {
        set_status(L"一括適応機能を利用できません。");
        return;
    }
    if (_samples.empty())
    {
        set_status(L"学習サンプルがありません。WAV と TXT のペアを保存してください。");
        return;
    }

    BOOL translated = FALSE;
    const UINT repetitions = GetDlgItemInt(_window, batch_count_edit_id, &translated, FALSE);
    if (!translated || repetitions == 0 || repetitions > 1000)
    {
        set_status(L"適応回数は1～1000の整数で指定してください。");
        return;
    }
    if (_samples.size() > 100000 / repetitions)
    {
        set_status(L"処理件数が上限の100,000件を超えています。回数を減らしてください。");
        return;
    }

    std::vector<std::wstring> texts;
    texts.reserve(_samples.size());
    for (training_sample& sample : _samples)
    {
        std::vector<std::uint8_t> wav_data;
        if (!storage_system::read_binary(sample.wav_path, wav_data) || wav_data.size() < 44 ||
            wav_data[0] != 'R' || wav_data[1] != 'I' || wav_data[2] != 'F' || wav_data[3] != 'F' ||
            wav_data[8] != 'W' || wav_data[9] != 'A' || wav_data[10] != 'V' || wav_data[11] != 'E')
        {
            set_status(L"WAV ファイルを読み込めない、または WAV 形式ではないサンプルがあります。");
            return;
        }

        const std::size_t extension = sample.wav_path.rfind(L".wav");
        if (extension == std::wstring::npos)
        {
            set_status(L"WAV ファイル名が不正です。");
            return;
        }
        const std::wstring text_path = sample.wav_path.substr(0, extension) + L".txt";
        std::wstring text;
        if (!storage_system::read_utf8_text(text_path, text) || text.empty() || text.size() > 512)
        {
            set_status(L"TXT を読み込めない、空、または512文字を超えるサンプルがあります。");
            return;
        }

        sample.text = text;
        texts.push_back(std::move(text));
    }

    if (_batch_adaptation_callback(texts, static_cast<std::size_t>(repetitions)))
    {
        set_status(L"全サンプルの一括適応を開始しました。SAPI の完了通知を待っています。");
    }
}

void training_window::open_sample_directory()
{
    std::wstring directory;
    if (!get_sample_directory(directory, true))
    {
        set_status(L"サンプル保存先フォルダーを作成できませんでした。");
        return;
    }

    const HINSTANCE result = ShellExecuteW(_window, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32)
    {
        set_status(L"エクスプローラーでサンプル保存先を開けませんでした。");
        return;
    }

    set_status(L"サンプル保存先をエクスプローラーで開きました。");
}

void training_window::update_selected_sample_text()
{
    const LRESULT selected = SendMessageW(_sample_list, LB_GETCURSEL, 0, 0);
    if (selected == LB_ERR || static_cast<std::size_t>(selected) >= _samples.size())
    {
        set_status(L"テキストを更新するサンプルを選択してください。");
        return;
    }

    const std::wstring text = get_correction_text();
    if (text.empty())
    {
        set_status(L"正解テキストを入力してください。");
        return;
    }

    training_sample& sample = _samples[static_cast<std::size_t>(selected)];
    const std::size_t extension = sample.wav_path.rfind(L".wav");
    if (extension == std::wstring::npos)
    {
        set_status(L"サンプルファイル名が不正です。");
        return;
    }
    const std::wstring text_path = sample.wav_path.substr(0, extension) + L".txt";
    if (!storage_system::write_utf8_text_atomic(text_path, text))
    {
        set_status(L"正解テキストを更新できませんでした。");
        return;
    }

    sample.text = text;
    set_status(L"選択中のサンプルの正解テキストを更新しました。");
}

std::wstring training_window::get_correction_text() const
{
    if (!_correction_edit) return {};
    const int length = GetWindowTextLengthW(_correction_edit);
    std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(_correction_edit, buffer.data(), length + 1);
    return std::wstring(buffer.data(), static_cast<std::size_t>((std::max)(copied, 0)));
}

bool training_window::save_training_text(const std::wstring& text) const
{
    const std::wstring path = storage_system::get_training_history_path(true);
    return !path.empty() && storage_system::append_utf8_text(path, text + L"\r\n", true);
}

void training_window::apply_font(HWND control)
{
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}
