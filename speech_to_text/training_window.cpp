#include "training_window.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

constexpr wchar_t training_window::_window_class_name[];

namespace
{
    constexpr int window_width = 680;
    constexpr int window_height = 460;
    constexpr int margin = 12;
    constexpr int control_height = 26;
    constexpr int label_height = 20;
}

training_window::training_window(HWND parent)
    : _parent(parent)
{
}

training_window::~training_window()
{
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
        case candidate_combo_id:
            if (HIWORD(w_param) == CBN_SELCHANGE)
            {
                const LRESULT record_index = SendMessageW(_record_list, LB_GETCURSEL, 0, 0);
                const LRESULT candidate_index = SendMessageW(_candidate_combo, CB_GETCURSEL, 0, 0);
                if (record_index != LB_ERR && candidate_index != CB_ERR)
                {
                    const speech_recognition_result& record = _records[static_cast<size_t>(record_index)];
                    const std::wstring& candidate = candidate_index == 0 ? record.text : record.alternatives[static_cast<size_t>(candidate_index - 1)];
                    SetWindowTextW(_correction_edit, candidate.c_str());
                }
            }
            return 0;
        case commit_button_id:
            if (HIWORD(w_param) == BN_CLICKED && _alternate_callback)
            {
                const LRESULT record_index = SendMessageW(_record_list, LB_GETCURSEL, 0, 0);
                const LRESULT candidate_index = SendMessageW(_candidate_combo, CB_GETCURSEL, 0, 0);
                if (record_index != LB_ERR && candidate_index > 0)
                {
                    const speech_recognition_result& record = _records[static_cast<size_t>(record_index)];
                    _alternate_callback(record.id, static_cast<size_t>(candidate_index - 1));
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
    _correction_label = CreateWindowExW(0, L"STATIC", L"訂正後の文章", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(correction_label_id)), instance, nullptr);
    _correction_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(correction_edit_id)), instance, nullptr);
    _commit_button = CreateWindowExW(0, L"BUTTON", L"候補を確定", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(commit_button_id)), instance, nullptr);
    _adaptation_button = CreateWindowExW(0, L"BUTTON", L"訂正内容を学習", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(adaptation_button_id)), instance, nullptr);
    _close_button = CreateWindowExW(0, L"BUTTON", L"閉じる", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(close_button_id)), instance, nullptr);
    _status_text = CreateWindowExW(0, L"STATIC", L"認識結果を選択して訂正できます。", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, _window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(status_text_id)), instance, nullptr);
    if (!_record_label || !_record_list || !_candidate_label || !_candidate_combo || !_correction_label || !_correction_edit || !_commit_button || !_adaptation_button || !_close_button || !_status_text) return false;

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
    load_training_history();
    refresh_records();
    return true;
}

void training_window::layout_controls(int width, int height)
{
    if (!_record_label || !_record_list || !_candidate_label || !_candidate_combo || !_correction_label || !_correction_edit || !_commit_button || !_adaptation_button || !_close_button || !_status_text) return;
    const int content_width = std::max(width - margin * 2, 120);
    const int list_label_y = margin;
    const int list_y = list_label_y + label_height;
    const int list_height = std::max(height / 2 - label_height, 80);
    const int candidate_y = list_y + list_height + margin;
    const int correction_y = candidate_y + label_height + control_height + margin;
    const int buttons_y = correction_y + label_height + control_height + margin;
    const int status_y = buttons_y + control_height + margin;
    const int button_width = 120;
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
    if (selected == LB_ERR || static_cast<size_t>(selected) >= _records.size()) return;
    const speech_recognition_result& record = _records[static_cast<size_t>(selected)];
    SendMessageW(_candidate_combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(_candidate_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(record.text.c_str()));
    for (const std::wstring& alternative : record.alternatives) SendMessageW(_candidate_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(alternative.c_str()));
    SendMessageW(_candidate_combo, CB_SETCURSEL, 0, 0);
    SetWindowTextW(_correction_edit, record.text.c_str());
}

std::wstring training_window::get_correction_text() const
{
    if (!_correction_edit) return {};
    const int length = GetWindowTextLengthW(_correction_edit);
    std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1, L'\0');
    const int copied = GetWindowTextW(_correction_edit, buffer.data(), length + 1);
    return std::wstring(buffer.data(), static_cast<size_t>(std::max(copied, 0)));
}

bool training_window::save_training_text(const std::wstring& text) const
{
    wchar_t app_data[MAX_PATH]{};
    const DWORD path_length = GetEnvironmentVariableW(L"APPDATA", app_data, MAX_PATH);
    if (path_length == 0 || path_length >= MAX_PATH) return false;

    const std::wstring directory = std::wstring(app_data) + L"\\VoiceLink";
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;

    const std::wstring file_path = directory + L"\\sapi_training.txt";
    HANDLE file = CreateFileW(file_path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER file_size{};
    const bool has_size = GetFileSizeEx(file, &file_size) != FALSE;
    bool success = has_size;
    if (success && file_size.QuadPart == 0)
    {
        const char bom[] = { static_cast<char>(0xEF), static_cast<char>(0xBB), static_cast<char>(0xBF) };
        DWORD written = 0;
        success = WriteFile(file, bom, sizeof(bom), &written, nullptr) != FALSE && written == sizeof(bom);
    }

    std::wstring line = text + L"\r\n";
    const int byte_count = WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), nullptr, 0, nullptr, nullptr);
    if (success && byte_count > 0)
    {
        std::vector<char> utf8(static_cast<size_t>(byte_count));
        const int converted = WideCharToMultiByte(CP_UTF8, 0, line.data(), static_cast<int>(line.size()), utf8.data(), byte_count, nullptr, nullptr);
        DWORD written = 0;
        success = converted == byte_count && WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr) != FALSE && written == static_cast<DWORD>(utf8.size());
    }
    else
    {
        success = false;
    }

    CloseHandle(file);
    return success;
}

void training_window::apply_font(HWND control)
{
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
}
